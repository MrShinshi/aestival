/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "chat_context_manager.h"
#include "chat_storage_sqlite.h"

#include "model_client.h"

namespace client {

// ─── constants ────────────────────────────────────────────────────────────
// Thresholds are in BYTES (UTF-8 encoded).  Chinese text ≈ 3 bytes/char,
// ≈ 0.75 tokens/char.  64KB ≈ 16K tokens ≈ 12% of DeepSeek 128K window.
static constexpr size_t kMaxTotalBytes = 65536;			 // trigger summarization
static constexpr size_t kConcatMaxBytes = 8192;			 // max chars in summary prompt
static constexpr size_t kKeepLast = 10;					 // recent messages to retain
static constexpr size_t k_summary_truncate_bytes = 1200; // max bytes for fallback truncation
static constexpr std::string_view kSummaryPrefix = "Conversation summary: ";

// ─── helpers ──────────────────────────────────────────────────────────────

// Is this a summary system message (not a user-defined system prompt)?
static bool is_summary_message(chat_message const& m) {
	if (m.role != "system")
		return false;
	return m.content.starts_with(kSummaryPrefix);
}

// ─── constructor ──────────────────────────────────────────────────────────

chat_context_manager::chat_context_manager(std::shared_ptr<chat_storage_backend> backend)
	: backend_(std::move(backend)) {
}

// ─── append_user ──────────────────────────────────────────────────────────

void chat_context_manager::append_user(std::string const& convo_id, std::string const& sender_nick,
									   std::string const& content) {
	std::lock_guard<std::mutex> lk(mutex_);
	backend_->append_message(convo_id, {.role = "user", .sender_nick = sender_nick, .content = content});
}

// ─── append_assistant ─────────────────────────────────────────────────────

void chat_context_manager::append_assistant(std::string const& convo_id, std::string const& content) {
	std::lock_guard<std::mutex> lk(mutex_);
	backend_->append_message(convo_id, {.role = "assistant", .content = content});
}

// ─── append_tool ─────────────────────────────────────────────────────────

void chat_context_manager::append_tool(std::string const& convo_id, std::string const& tool_call_id,
									   std::string const& content) {
	std::lock_guard<std::mutex> lk(mutex_);
	chat_message m{"tool", "", content, tool_call_id, ""};
	backend_->append_message(convo_id, m);
}

// ─── append_assistant_with_tool_calls ─────────────────────────────────────

void chat_context_manager::append_assistant_with_tool_calls(std::string const& convo_id, std::string const& content,
															std::string const& tool_calls_json) {
	std::lock_guard<std::mutex> lk(mutex_);
	chat_message m{"assistant", "", content, "", tool_calls_json};
	backend_->append_message(convo_id, m);
}

// ─── get_messages ─────────────────────────────────────────────────────────

std::vector<chat_message> chat_context_manager::get_messages(std::string const& convo_id) {
	std::lock_guard<std::mutex> lk(mutex_);
	return backend_->load(convo_id);
}

// ─── summarize_with_model ─────────────────────────────────────────────────
// Load -> check size -> unlock -> LLM -> lock -> reload -> save.
//
// The LLM call runs WITHOUT holding mutex_ so other threads can continue
// reading/writing.  Concurrent modifications are handled safely:
//   - REMOVED (e.g. "clear") → skip save (concurrent modification wins)
//   - APPENDED (new messages arrived) → preserved verbatim; only the
//     original pre-LLM range is summarized.
//
// Old "Conversation summary: ..." system messages are dropped — only the
// freshest summary is kept to prevent summary pile-up.

void chat_context_manager::summarize_with_model(std::string const& convo_id, model_client& client) {
	std::vector<chat_message> messages;
	std::string prompt;
	size_t msg_count_before = 0;
	{
		std::lock_guard<std::mutex> lk(mutex_);
		messages = backend_->load(convo_id);
		msg_count_before = messages.size();
		size_t total = 0;
		for (auto const& m : messages)
			total += m.content.size();
		if (total <= kMaxTotalBytes)
			return;

		std::string concat;
		// Include the most recent summary (if any) in the prompt so the LLM
		// can build on it rather than re-deriving from scratch.
		for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
			if (is_summary_message(*it)) {
				concat += "[previous summary] " + it->content.substr(kSummaryPrefix.size()) + "\n";
				break;
			}
		}
		for (auto const& m : messages) {
			if (m.role == "system")
				continue;
			concat += "[" + m.role + "] " + m.content + "\n";
			if (concat.size() > kConcatMaxBytes)
				break;
		}
		prompt =
			std::string("请将以下对话摘要为一段简洁的中文总结，保留重要事实、实体和用户偏好。只返回摘要文本：\n\n") +
			concat;
	}

	// Step 2: call LLM WITHOUT holding the global lock
	try {
		std::string summary = client.complete(prompt);
		if (summary.empty())
			return;

		// Step 3: re-acquire lock to save the result.
		std::lock_guard<std::mutex> lk(mutex_);
		messages = backend_->load(convo_id);
		if (messages.size() < msg_count_before)
			return; // clear happened

		// Build reduced from the ORIGINAL range (0..msg_count_before-1).
		// Messages that arrived during the LLM call (i >= msg_count_before)
		// are appended verbatim — they must never be dropped.
		std::vector<chat_message> reduced;

		// Keep non-summary system messages (user-defined prompts).
		// Old summaries are dropped — only the new one below is kept.
		for (size_t i = 0; i < msg_count_before && i < messages.size(); ++i) {
			if (messages[i].role == "system" && !is_summary_message(messages[i]))
				reduced.push_back(messages[i]);
		}

		// Insert the fresh summary as a system message.
		reduced.push_back({"system", "", std::string(kSummaryPrefix) + summary});

		// Keep the last kKeepLast non-system messages from original range.
		size_t keep_start = msg_count_before > kKeepLast ? msg_count_before - kKeepLast : 0;
		for (size_t i = keep_start; i < msg_count_before && i < messages.size(); ++i) {
			if (messages[i].role == "system")
				continue;
			reduced.push_back(messages[i]);
		}

		// Append every message that arrived during the LLM call (unmodified).
		for (size_t i = msg_count_before; i < messages.size(); ++i)
			reduced.push_back(messages[i]);

		backend_->save(convo_id, reduced);
	} catch (...) {
		std::lock_guard<std::mutex> lk(mutex_);
		auto fallback_msgs = backend_->load(convo_id);
		summarize_if_needed(fallback_msgs);
		backend_->save(convo_id, fallback_msgs);
	}
}

// ─── clear ────────────────────────────────────────────────────────────────

void chat_context_manager::clear(std::string const& convo_id) {
	std::lock_guard<std::mutex> lk(mutex_);
	backend_->clear_messages(convo_id);
}

// ─── summarize_if_needed (length-based truncation, no model call) ─────────
// Operates on in-memory vector; caller holds mutex_ and calls save afterwards.

void chat_context_manager::summarize_if_needed(std::vector<chat_message>& messages) {
	size_t total = 0;
	for (auto const& m : messages)
		total += m.content.size();
	if (total <= kMaxTotalBytes)
		return;

	std::string acc;
	// If there's already a summary, include it so we don't lose it.
	for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
		if (is_summary_message(*it)) {
			acc += it->content.substr(kSummaryPrefix.size()) + "\n";
			break;
		}
	}
	size_t idx = 0;
	for (; idx < messages.size(); ++idx) {
		if (messages[idx].role == "system")
			continue;
		if (acc.size() + messages[idx].content.size() > k_summary_truncate_bytes)
			break;
		acc += messages[idx].content + " ";
	}
	if (acc.empty())
		return;

	std::vector<chat_message> reduced;
	// Keep only non-summary system messages.
	for (auto const& m : messages) {
		if (m.role == "system" && !is_summary_message(m))
			reduced.push_back(m);
	}
	reduced.push_back({"system", "", std::string(kSummaryPrefix) + acc});
	for (size_t i = idx; i < messages.size(); ++i) {
		if (messages[i].role == "system")
			continue;
		reduced.push_back(messages[i]);
	}
	messages.swap(reduced);
}

// ─── token usage tracking ─────────────────────────────────────────────────

void chat_context_manager::record_token_usage(std::string const& model, int prompt_tokens, int completion_tokens) {
	auto s = std::dynamic_pointer_cast<sqlite_backend>(backend_);
	if (s)
		s->record_token_usage(model, prompt_tokens, completion_tokens);
}

std::vector<std::tuple<std::string, int, int64_t, int64_t>> chat_context_manager::get_token_stats() {
	auto s = std::dynamic_pointer_cast<sqlite_backend>(backend_);
	if (s)
		return s->get_token_stats();
	return {};
}

} // namespace client
