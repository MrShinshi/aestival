/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "bot_config.h"
#include "log.h"

namespace {

client::runtime_mode parse_mode(const std::string& value) {
	if (value == "agent") {
		return client::runtime_mode::agent;
	}

	if (!value.empty()) {
		client::log::warn("Unknown mode '" + value + "' in config, falling back to plugin mode");
	}

	return client::runtime_mode::plugin;
}

} // namespace

client::bot_config client::load_bot_config(const std::string& path) {
	std::ifstream stream(path);
	if (!stream) {
		throw std::runtime_error("failed to open config file: " + path);
	}

	nlohmann::json root = nlohmann::json::parse(stream, nullptr, true, true);

	bot_config config;
	if (const auto qq = root.find("qq"); qq != root.end() && qq->is_object()) {
		config.qq_app_id = qq->value("app_id", "");
		config.qq_app_secret = qq->value("app_secret", "");
	}

	if (const auto deepseek = root.find("deepseek"); deepseek != root.end() && deepseek->is_object()) {
		config.deepseek_api_key = deepseek->value("api_key", "");
		config.deepseek_model = deepseek->value("model", config.deepseek_model);
		config.deepseek_user_token = deepseek->value("user_token", "");
		config.deepseek_waf_cookie = deepseek->value("waf_cookie", "");
	}

	if (const auto openai = root.find("openai"); openai != root.end() && openai->is_object()) {
		config.openai_api_key = openai->value("api_key", "");
		config.openai_model = openai->value("model", config.openai_model);
		config.openai_base_url = openai->value("base_url", config.openai_base_url);
	}

	// LLM provider selection
	config.llm_provider = root.value("llm_provider", "");

	if (const auto admins = root.find("admins"); admins != root.end() && admins->is_array()) {
		for (const auto& item : *admins) {
			if (item.is_string()) {
				config.admin_user_ids.push_back(item.get<std::string>());
			}
		}
	}

	config.workspace = root.value("workspace", config.workspace);
	config.storage_dir = root.value("storage_dir", config.storage_dir);
	config.default_mode = parse_mode(root.value("mode", "plugin"));
	config.agent_reach_enabled = root.value("agent_reach_enabled", config.agent_reach_enabled);
	config.max_messages_per_minute = root.value("max_messages_per_minute", config.max_messages_per_minute);
	config.daily_token_budget = root.value("daily_token_budget", config.daily_token_budget);
	config.verify_tls = root.value("verify_tls", config.verify_tls);
	config.log_file = root.value("log_file", config.log_file);

	// Self-iteration config
	if (const auto si = root.find("self_iteration"); si != root.end() && si->is_object()) {
		config.self_iterate_enabled = si->value("enabled", config.self_iterate_enabled);
		config.self_iterate_interval_hours = si->value("interval_hours", config.self_iterate_interval_hours);
		config.self_iterate_min_conversations = si->value("min_conversations", config.self_iterate_min_conversations);
		config.claude_code_path = si->value("claude_path", config.claude_code_path);
	}

	if (config.qq_app_id.empty() || config.qq_app_secret.empty()) {
		client::log::warn("QQ credentials are missing — QQ mode will fail at startup; console mode is unaffected");
	}

	return config;
}
