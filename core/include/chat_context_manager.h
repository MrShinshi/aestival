/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

namespace client {

struct chat_message {
	std::string role;		 // "user" | "assistant" | "system" | "tool"
	std::string sender_nick; // display name (empty for system/assistant/tool)
	std::string content;
	std::string tool_call_id;	 // for "tool" role: the call id being responded to
	std::string tool_calls_json; // for "assistant" role: JSON array of tool_calls
};

struct tool_call {
	std::string id;
	std::string function_name;
	std::string arguments; // JSON string
};

// ─── abstract storage backend ────────────────────────────────────────────
struct chat_storage_backend {
	virtual ~chat_storage_backend() = default;
	virtual std::vector<chat_message> load(std::string const& convo_id) = 0;
	virtual void save(std::string const& convo_id, std::vector<chat_message> const& msgs) = 0;
	virtual void append_message(std::string const& convo_id, chat_message const& msg) = 0;
	virtual void clear_messages(std::string const& convo_id) = 0;
	virtual int64_t count(std::string const& convo_id) = 0;
};

// forward declare model_client to avoid header dependency
struct model_client;

struct chat_context_manager {
	public:
	explicit chat_context_manager(std::shared_ptr<chat_storage_backend> backend);

	// Append messages and persist
	void append_user(std::string const& convo_id, std::string const& sender_nick, std::string const& content);
	void append_assistant(std::string const& convo_id, std::string const& content);

	// Persist tool messages (tool call + tool result) so they survive across turns.
	void append_tool(std::string const& convo_id, std::string const& tool_call_id, std::string const& content);
	void append_assistant_with_tool_calls(std::string const& convo_id, std::string const& content,
										  std::string const& tool_calls_json);

	// Wipe conversation history
	void clear(std::string const& convo_id);

	// Get all messages for a conversation
	std::vector<chat_message> get_messages(std::string const& convo_id);

	// Use model to produce a semantic summary when available
	void summarize_with_model(std::string const& convo_id, model_client& client);

	// Token usage tracking (pass-through to backend)
	void record_token_usage(std::string const& model, int prompt_tokens, int completion_tokens);
	std::vector<std::tuple<std::string, int, int64_t, int64_t>> get_token_stats();

	private:
	std::shared_ptr<chat_storage_backend> backend_;
	std::mutex mutex_;

	void summarize_if_needed(std::vector<chat_message>& messages);
};

} // namespace client
