/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "policy_engine.h"
#include "encode_utils.h"
#include "log.h"

namespace client {

namespace {

// ─── magic-number constants ─────────────────────────────────────────────

static constexpr size_t kMaxMessageLength = 8000; // max input chars
static constexpr int kCharRepeatThreshold = 100;  // same-char spam detection
static constexpr int kRateWindowSeconds = 60;	  // per-user rate-limit window
static constexpr size_t kRateGcThreshold = 1000;  // trigger stale-entry GC
static constexpr int kRateGcStaleSeconds = 120;	  // entries older than this are stale

// ─── atomic day-reset helper ───────────────────────────────────────────
// Uses compare_exchange to avoid TOCTOU race between check and reset.

void atomic_day_reset(std::atomic<int>& day, std::atomic<int64_t>& counter) {
	int today = policy_engine::today_day_of_year();
	int expected = day.load();
	if (expected != today) {
		// Only one thread wins the CAS; losers see the updated day next time.
		if (day.compare_exchange_strong(expected, today)) {
			counter.store(0);
		}
	}
}

} // namespace

// ─── constructor ──────────────────────────────────────────────────────────

policy_engine::policy_engine(config cfg) : cfg_(std::move(cfg)) {
}

// ─── today_day_of_year ────────────────────────────────────────────────────

int policy_engine::today_day_of_year() {
	auto now = std::chrono::system_clock::now();
	auto tt = std::chrono::system_clock::to_time_t(now);
	std::tm tm{};
#ifdef _WIN32
	localtime_s(&tm, &tt);
#else
	localtime_r(&tt, &tm);
#endif
	return tm.tm_yday;
}

// ─── check_input ──────────────────────────────────────────────────────────
// Detect prompt injection, excessive length, and suspicious patterns.

bool policy_engine::check_input(message_event const& msg, std::string* reason) {
	auto const& c = msg.content;

	// ── empty ──────────────────────────────────────────────────────────
	if (c.empty()) {
		if (reason)
			*reason = "空白消息";
		return false;
	}

	// ── excessive length ───────────────────────────────────────────────
	if (c.size() > kMaxMessageLength) {
		if (reason)
			*reason = "消息过长（>" + std::to_string(kMaxMessageLength) + "字符）";
		log::warn("[policy] blocked: excessive length " + std::to_string(c.size()));
		return false;
	}

	// ── prompt injection patterns ──────────────────────────────────────
	// Simple but effective: detect common injection phrases.
	static const char* kInjectionPatterns[] = {
		"ignore all previous instructions",
		"ignore previous instructions",
		"ignore the above",
		"disregard previous",
		"forget all previous",
		"new system prompt",
		"you are now",
		"your new role is",
		"from now on you are",
		"<|im_start|>",
		"<|im_end|>",
		"<|system|>",
		"<|user|>",
		"<|assistant|>",
		"[/INST]",
		"[INST]",
		"<<SYS>>",
		"<</SYS>>",
	};

	auto lower = client::to_ascii_lower(c);
	for (auto* pat : kInjectionPatterns) {
		if (lower.find(pat) != std::string::npos) {
			log::warn("[policy] blocked: injection pattern '" + std::string(pat) + "'");
			if (reason) {
				*reason = std::string("检测到提示注入模式: ") + pat;
			}
			return false;
		}
	}

	// ── excessive repetition ───────────────────────────────────────────
	// Detect spam: same character repeated 100+ times.
	char last = 0;
	int run = 0;
	for (char ch : c) {
		if (ch == last) {
			++run;
			if (run > kCharRepeatThreshold)
				break;
		} else {
			last = ch;
			run = 1;
		}
	}
	if (run > kCharRepeatThreshold) {
		if (reason)
			*reason = "检测到字符重复刷屏";
		log::warn("[policy] blocked: character spam");
		return false;
	}

	return true;
}

// ─── check_llm_call ───────────────────────────────────────────────────────

bool policy_engine::check_llm_call(std::string const& actor_id, std::string* reason) {
	// ── token budget (daily) ───────────────────────────────────────────
	atomic_day_reset(token_check_day_, tokens_used_today_);

	if (cfg_.daily_token_budget > 0) {
		int64_t used = tokens_used_today_.load();
		if (used >= cfg_.daily_token_budget) {
			if (reason)
				*reason = "今日 token 配额已用完";
			log::warn("[policy] token budget exhausted: " + std::to_string(used));
			return false;
		}
	}

	// ── rate limit (per-user per-minute) ───────────────────────────────
	if (cfg_.max_messages_per_minute > 0) {
		std::lock_guard<std::mutex> lk(rate_mutex_);
		auto& state = rate_limits_[actor_id];
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - state.window_start).count();

		if (elapsed >= kRateWindowSeconds || elapsed < 0) {
			// New window
			state.window_start = now;
			state.count = 1;
		} else {
			++state.count;
			if (state.count > cfg_.max_messages_per_minute) {
				if (reason)
					*reason = "消息发送过快，请稍后再试";
				log::info("[policy] rate limited: " + actor_id);
				return false;
			}
		}

		// Garbage-collect stale entries periodically.
		if (rate_limits_.size() > kRateGcThreshold) {
			auto old = now - std::chrono::seconds(kRateGcStaleSeconds);
			for (auto it = rate_limits_.begin(); it != rate_limits_.end();) {
				if (it->second.window_start < old)
					it = rate_limits_.erase(it);
				else
					++it;
			}
		}
	}

	return true;
}

// ─── filter_output ────────────────────────────────────────────────────────

std::string policy_engine::filter_output(std::string_view content) {
	// ── strip internal markers ─────────────────────────────────────────
	// Remove [SEARCH:] and [FETCH:] tags that might leak from LLM output.
	// These are internal tool-call triggers, not user-facing content.
	std::string s(content);

	// Strip entire [SEARCH:...] and [FETCH:...] blocks that weren't
	// intercepted by tool_loop (edge case).
	static auto const kTagRe = [] {
		try {
			return boost::regex(R"(\[(SEARCH|FETCH):[^\]]*\])");
		} catch (...) {
			return boost::regex("");
		}
	}();
	try {
		s = boost::regex_replace(s, kTagRe, "");
	} catch (...) {
	}

	// ── strip system token leaks ───────────────────────────────────────
	if (s.find("<|im_start|>") != std::string::npos || s.find("<|im_end|>") != std::string::npos ||
		s.find("<|system|>") != std::string::npos) {
		log::warn("[policy] LLM output contained system tokens — blocking");
		return {};
	}

	return s;
}

// ─── sanitize_reply ───────────────────────────────────────────────────────

std::string policy_engine::sanitize_reply(std::string_view content, size_t max_len) {
	std::string result;
	result.reserve(std::min(content.size(), max_len));

	for (char ch : content) {
		if (ch == '\0')
			continue;
		result.push_back(ch);
		if (result.size() >= max_len)
			break;
	}

	return sanitize_utf8(result);
}

// ─── record_tokens ────────────────────────────────────────────────────────

void policy_engine::record_tokens(int64_t prompt, int64_t completion) {
	atomic_day_reset(token_check_day_, tokens_used_today_);
	tokens_used_today_.fetch_add(prompt + completion);
}

// ─── tokens_remaining ─────────────────────────────────────────────────────

int64_t policy_engine::tokens_remaining() const {
	if (cfg_.daily_token_budget <= 0)
		return INT64_MAX;
	int64_t used = tokens_used_today_.load();
	return std::max<int64_t>(0, cfg_.daily_token_budget - used);
}

// ─── reset_all_limits ─────────────────────────────────────────────────────

void policy_engine::reset_all_limits() {
	{
		std::lock_guard<std::mutex> lk(rate_mutex_);
		rate_limits_.clear();
	}
	tokens_used_today_.store(0);
	token_check_day_.store(0);
	log::info("[policy] all limits reset");
}

} // namespace client
