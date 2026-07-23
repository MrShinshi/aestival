/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "runtime_mode.h"

#include <cstdint>
#include <string>
#include <vector>

namespace client {

// ─── agent_config ──────────────────────────────────────────────────────────
// Per-agent settings.  Each agent instance owns its platform session,
// LLM backend, workspace, policy, and self-iteration config.
//
// Loaded from bot_config.json → agents[] array.

struct agent_config {
	// ── identity ──────────────────────────────────────────────────────
	std::string id;	  // unique key, e.g. "feiying-qq"
	std::string name; // display name, e.g. "绯英"
	bool enabled = true;

	// ── platform ──────────────────────────────────────────────────────
	// "qq" | "console" | (future: "wechat", "discord")
	std::string platform = "qq";

	// QQ-specific (used when platform == "qq")
	std::string qq_app_id;
	std::string qq_app_secret;

	// ── LLM ───────────────────────────────────────────────────────────
	// "deepseek" | "openai" | "" (auto: use deepseek)
	std::string llm_provider = "deepseek";

	// DeepSeek
	std::string deepseek_api_key;
	std::string deepseek_model = "deepseek-chat";
	std::string deepseek_user_token;
	std::string deepseek_waf_cookie;

	// OpenAI / compatible endpoint
	std::string openai_api_key;
	std::string openai_model = "gpt-4o";
	std::string openai_base_url = "https://api.openai.com";

	// ── paths ─────────────────────────────────────────────────────────
	std::string workspace = "./workspace";
	std::string storage_dir = "./contexts";

	// ── access control ────────────────────────────────────────────────
	std::vector<std::string> admin_user_ids;

	// ── runtime ───────────────────────────────────────────────────────
	runtime_mode default_mode = runtime_mode::agent;
	bool agent_reach_enabled = true;

	// ── policy / safety ───────────────────────────────────────────────
	int max_messages_per_minute = 10;
	int64_t daily_token_budget = 0; // 0 = unlimited

	// ── self-iteration (Claude Code powered) ──────────────────────────
	bool self_iterate_enabled = false;
	int self_iterate_interval_hours = 24; // 0 = disabled
	int self_iterate_min_conversations = 10;
	std::string claude_code_path = "claude";
};

// ─── global_config ─────────────────────────────────────────────────────────
// Process-wide settings that apply to all agents or the bot process itself.

struct global_config {
	bool verify_tls = true;
	std::string log_file;

	// Management API (Phase 2)
	bool management_api_enabled = false;
	std::string management_listen = "127.0.0.1:9090";
	std::string jwt_secret;
};

// ─── bot_config ────────────────────────────────────────────────────────────
// Top-level container.  The loader supports both the new "agents" array
// format AND the legacy single-agent flat format (auto-converted on load).

struct bot_config {
	std::vector<agent_config> agents;
	global_config global;

	// True if the config file used the legacy flat format.  When saving
	// (dynamic add/remove), the new format is always written.
	bool is_legacy = false;
};

// ─── loader ────────────────────────────────────────────────────────────────

bot_config load_bot_config(std::string const& path);

// Serialise current config back to JSON (used for dynamic add/remove).
std::string to_json(bot_config const& cfg);
void save_bot_config(std::string const& path, bot_config const& cfg);

} // namespace client
