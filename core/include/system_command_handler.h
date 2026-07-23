/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "bot_messaging.h"
#include "chat_context_manager.h"
#include "runtime_mode.h"
#include "message_types.h"

#include <functional>
#include <mutex>
#include <string>
#include <unordered_set>

namespace client {

struct model_client;
struct agent_registry;

// ─── system_command_deps ─────────────────────────────────────────────────
// All external state that handle_system_command needs.

struct system_command_deps {
	bot_messaging& bot;
	model_client* llm = nullptr; // may be null
	chat_context_manager& contexts;
	std::string const& storage_dir;
	std::unordered_set<std::string> const& admin_ids;

	runtime_mode& mode; // mutable — "switch mode" writes
	std::mutex& mode_mutex;

	// For agent management commands (Phase 1 multi-agent).
	agent_registry* registry = nullptr;

	// Callback for "stop" command
	std::function<void()> on_stop;

	// Callback for "self-iterate" command (dry_run → Markdown reply)
	std::function<std::string(bool)> on_self_iterate;

	// How to derive a stable conversation ID from a message_event
	std::function<std::string(message_event const&)> actor_id_of;

	// How to send a reply for a given message
	std::function<bool(message_event const&, std::string_view)> reply_to;
};

// ─── system_command_handler ────────────────────────────────────────────────
// Extracted from agent_controller::handle_system_command.
// Pure logic — no threading, no ownership.

struct system_command_handler {
	// Returns true if `msg.content` matched a system command and was
	// fully handled (reply already sent).  Returns false to let the
	// message fall through to plugins / agent mode.
	static bool handle(std::string const& normalized_cmd, message_event const& msg, system_command_deps const& deps);

	// Check if a message sender is an admin.
	static bool is_admin(message_event const& msg, std::unordered_set<std::string> const& admin_ids);
};

} // namespace client
