/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "bot_messaging.h"
#include "bot_config.h"   // agent_config
#include "mcp_client.h"
#include "model_client.h"
#include "chat_context_manager.h"
#include "policy_engine.h"
#include "plugin.h"
#include "plugin_manager.h"
#include "token_counter.h"
#include "tool_registry.h"
#include "worker_pool.h"

#include <nlohmann/json.hpp>

namespace client {

struct agent_controller : std::enable_shared_from_this<agent_controller> {
	public:
	agent_controller(bot_messaging& bot, plugin_manager& plugins, std::shared_ptr<model_client> llm,
					 agent_config const& config);
	~agent_controller();

	void handle_message(message_event const& message);
	void notify_startup();

	// Self-iteration callback.
	std::function<std::string(bool dry_run)> on_self_iterate;

	private:
	// ── agent-mode message processing ──────────────────────────────────
	void process_agent_message(message_event const& message);

	// LLM-driven tool-using loop.
	void tool_loop(std::vector<chat_message>& messages, std::string const& convo_id);

	// Reply helper.
	bool reply_to(message_event const& message, std::string_view content);

	// Build message list with system prompts.
	std::vector<chat_message> build_message_list(std::string const& convo_id);

	// Derive stable convo_id from a message event.
	static std::string actor_id_of(message_event const& message);

	// Token tracking.
	void record_token_usage(nlohmann::json const& usage);

	// ── owned modules ──────────────────────────────────────────────────
	bot_messaging& bot_;
	plugin_manager& plugins_;
	std::shared_ptr<model_client> llm_;
	policy_engine policy_;
	tool_registry tools_;
	chat_context_manager chat_contexts_;
	worker_pool workers_;
	std::vector<chat_message> global_system_;
	std::string group_rules_template_; // GROUP_RULES.md with {AT_HINT} placeholder
	std::string storage_dir_;
	std::unordered_set<std::string> admin_ids_;

	mutable std::mutex state_mutex_;
	runtime_mode mode_ = runtime_mode::plugin;
	std::atomic<bool> stopping_{false};

	// Active MCP clients (owned for lifecycle, tools registered in tool_registry).
	std::vector<std::shared_ptr<mcp_client>> mcp_clients_;

	static constexpr size_t kAtHintLen = 9; // length of "{AT_HINT}"
};

} // namespace client
