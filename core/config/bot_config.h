/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "runtime_mode.h"

#include <string>
#include <vector>

namespace client {

struct bot_config {
	// ── QQ credentials ──────────────────────────────────────────────
	std::string qq_app_id;
	std::string qq_app_secret;

	// ── LLM provider selection ──────────────────────────────────────
	// "deepseek" | "openai" | "" (auto: use legacy deepseek key)
	std::string llm_provider;

	// DeepSeek
	std::string deepseek_api_key;
	std::string deepseek_model = "deepseek-chat";
	std::string deepseek_user_token; // platform.deepseek.com browser token
	std::string deepseek_waf_cookie; // HWWAFSESTIME + HWWAFSESID

	// OpenAI (or any OpenAI-compatible endpoint)
	std::string openai_api_key;
	std::string openai_model = "gpt-4o";
	std::string openai_base_url = "https://api.openai.com";

	// ── paths ──────────────────────────────────────────────────────
	std::string workspace = "./workspace";
	std::string storage_dir = "./contexts";

	// ── access control ─────────────────────────────────────────────
	std::vector<std::string> admin_user_ids;

	// ── runtime ────────────────────────────────────────────────────
	runtime_mode default_mode = runtime_mode::plugin;
	bool agent_reach_enabled = true;
	bool verify_tls = true;

	// ── self-iteration (Claude Code powered) ───────────────────────
	bool self_iterate_enabled = false;
	int self_iterate_interval_hours = 24; // 0 = disabled
	int self_iterate_min_conversations = 10;
	std::string claude_code_path = "claude";

	// ── policy / safety ─────────────────────────────────────────
	int max_messages_per_minute = 10;
	int64_t daily_token_budget = 0; // 0 = unlimited

	std::string log_file;
};

bot_config load_bot_config(const std::string& path);

} // namespace client
