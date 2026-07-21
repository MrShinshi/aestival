/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "bot_config.h"
#include "message_types.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace client {

// ─── policy_engine ───────────────────────────────────────────────────────
//
// Deterministic safety layer that runs BELOW the LLM.  All checks are
// rule-based — no model calls — so they're fast and predictable.
//
// Four checkpoints intercept every message lifecycle:
//   pre_input  — before plugin/agent processing
//   pre_llm    — before every LLM API call
//   post_llm   — after LLM returns, before reply
//   pre_reply  — final output sanitization

struct policy_engine {
	// ── config ──────────────────────────────────────────────────────────

	struct config {
		int max_messages_per_minute = 10; // per-user rate limit
		int max_turns_per_convo = 50;	  // prevent context explosion
		int64_t daily_token_budget = 0;	  // 0 = unlimited
	};

	explicit policy_engine(config cfg);
	policy_engine() = default;

	// ── checkpoints ─────────────────────────────────────────────────────

	// pre_input: scan incoming message for prompt injection / abuse.
	// Returns false if the message should be BLOCKED.
	// Sets `reason` to a human-readable explanation.
	bool check_input(message_event const& msg, std::string* reason = nullptr);

	// pre_llm: enforce rate limits and token budget before every API call.
	// Returns false if the call should be SKIPPED.
	bool check_llm_call(std::string const& actor_id, std::string* reason = nullptr);

	// post_llm: filter LLM output for safety issues.
	// Returns sanitized content (or empty string to block).
	std::string filter_output(std::string_view content);

	// pre_reply: final truncation / escaping before sending to platform.
	std::string sanitize_reply(std::string_view content, size_t max_len = 4096);

	// ── token budget tracking ───────────────────────────────────────────

	void record_tokens(int64_t prompt, int64_t completion);
	int64_t tokens_used_today() const {
		return tokens_used_today_.load();
	}
	int64_t tokens_remaining() const;

	// ── rate limit reset ────────────────────────────────────────────────
	void reset_all_limits();

	// Day-of-year helper (used by atomic_day_reset in .cpp)
	static int today_day_of_year();

	private:
	config cfg_;

	// Rate limiting: maps actor_id → (window_start, count)
	struct rate_state {
		std::chrono::steady_clock::time_point window_start;
		int count = 0;
	};
	std::unordered_map<std::string, rate_state> rate_limits_;
	mutable std::mutex rate_mutex_;

	// Token budget
	std::atomic<int64_t> tokens_used_today_{0};
	std::atomic<int> token_check_day_{0}; // day-of-year for reset
};

} // namespace client
