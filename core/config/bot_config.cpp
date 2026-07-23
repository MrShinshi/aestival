/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "bot_config.h"
#include "log.h"

namespace {

// ─── helpers ────────────────────────────────────────────────────────────────

client::runtime_mode parse_mode(std::string const& value) {
	if (value == "agent")
		return client::runtime_mode::agent;
	if (!value.empty())
		client::log::warn("Unknown mode '" + value + "' in config, falling back to plugin mode");
	return client::runtime_mode::plugin;
}

std::vector<std::string> parse_string_array(nlohmann::json const& obj, char const* key) {
	std::vector<std::string> out;
	auto it = obj.find(key);
	if (it == obj.end() || !it->is_array())
		return out;
	for (auto const& item : *it)
		if (item.is_string())
			out.push_back(item.get<std::string>());
	return out;
}

// ─── parse one agent from a JSON object ────────────────────────────────────

client::agent_config parse_agent(nlohmann::json const& j) {
	client::agent_config a;

	a.id = j.value("id", "");
	a.name = j.value("name", "");
	a.enabled = j.value("enabled", true);
	a.platform = j.value("platform", "qq");

	// QQ credentials
	if (auto qq = j.find("qq"); qq != j.end() && qq->is_object()) {
		a.qq_app_id = qq->value("app_id", "");
		a.qq_app_secret = qq->value("app_secret", "");
	}

	// LLM
	a.llm_provider = j.value("llm_provider", "deepseek");
	if (auto ds = j.find("deepseek"); ds != j.end() && ds->is_object()) {
		a.deepseek_api_key = ds->value("api_key", "");
		a.deepseek_model = ds->value("model", a.deepseek_model);
		a.deepseek_user_token = ds->value("user_token", "");
		a.deepseek_waf_cookie = ds->value("waf_cookie", "");
	}
	if (auto oa = j.find("openai"); oa != j.end() && oa->is_object()) {
		a.openai_api_key = oa->value("api_key", "");
		a.openai_model = oa->value("model", a.openai_model);
		a.openai_base_url = oa->value("base_url", a.openai_base_url);
	}

	// Paths
	a.workspace = j.value("workspace", a.workspace);
	a.storage_dir = j.value("storage_dir", a.storage_dir);

	// Admins
	a.admin_user_ids = parse_string_array(j, "admins");

	// Runtime
	a.default_mode = parse_mode(j.value("mode", "agent"));
	a.agent_reach_enabled = j.value("agent_reach_enabled", a.agent_reach_enabled);

	// Policy
	a.max_messages_per_minute = j.value("max_messages_per_minute", a.max_messages_per_minute);
	a.daily_token_budget = j.value("daily_token_budget", a.daily_token_budget);

	// Self-iteration
	if (auto si = j.find("self_iteration"); si != j.end() && si->is_object()) {
		a.self_iterate_enabled = si->value("enabled", a.self_iterate_enabled);
		a.self_iterate_interval_hours = si->value("interval_hours", a.self_iterate_interval_hours);
		a.self_iterate_min_conversations = si->value("min_conversations", a.self_iterate_min_conversations);
		a.claude_code_path = si->value("claude_path", a.claude_code_path);
	}

	return a;
}

// ─── parse global section ──────────────────────────────────────────────────

client::global_config parse_global(nlohmann::json const& root) {
	client::global_config g;
	g.verify_tls = root.value("verify_tls", true);
	g.log_file = root.value("log_file", "");

	if (auto mgmt = root.find("management_api"); mgmt != root.end() && mgmt->is_object()) {
		g.management_api_enabled = mgmt->value("enabled", false);
		g.management_listen = mgmt->value("listen", g.management_listen);
		g.jwt_secret = mgmt->value("jwt_secret", "");
	}

	return g;
}

} // namespace

// ─── load_bot_config ────────────────────────────────────────────────────────
//
// Supports two formats:
//
// 1) New format (preferred):
//    { "agents": [...], "global": {...} }
//
// 2) Legacy flat format:
//    { "qq": {...}, "deepseek": {...}, "admins": [...], "mode": "agent", ... }
//    → auto-converted to a single agent_config with id "default".

client::bot_config client::load_bot_config(std::string const& path) {
	std::ifstream stream(path);
	if (!stream)
		throw std::runtime_error("failed to open config file: " + path);

	nlohmann::json root = nlohmann::json::parse(stream, nullptr, true, true);
	bot_config cfg;

	// ── global section (present in both formats) ──────────────────────
	cfg.global = parse_global(root);

	// ── agents array (new format) ─────────────────────────────────────
	auto agents_it = root.find("agents");
	if (agents_it != root.end() && agents_it->is_array()) {
		for (auto const& item : *agents_it) {
			if (!item.is_object())
				continue;
			auto a = parse_agent(item);
			if (a.id.empty()) {
				client::log::warn("[config] agent entry missing 'id' — skipped");
				continue;
			}
			cfg.agents.push_back(std::move(a));
		}
		cfg.is_legacy = false;
	} else {
		// ── legacy flat format: convert to single agent ────────────────
		client::log::warn("[config] using legacy flat format — auto-converting to single agent 'default'");
		cfg.is_legacy = true;
		cfg.agents.push_back(parse_agent(root));
		if (cfg.agents.back().id.empty())
			cfg.agents.back().id = "default";
		if (cfg.agents.back().name.empty())
			cfg.agents.back().name = "Default";
	}

	if (cfg.agents.empty())
		throw std::runtime_error("no agents defined in config");

	// Friendly warning when QQ will fail
	for (auto const& a : cfg.agents) {
		if (a.platform == "qq" && (a.qq_app_id.empty() || a.qq_app_secret.empty()))
			client::log::warn("[config] agent '" + a.id +
							  "': QQ credentials missing — will fail at startup; console agents unaffected");
	}

	return cfg;
}

// ─── to_json ────────────────────────────────────────────────────────────────
// Always writes the new "agents" array format.

std::string client::to_json(bot_config const& cfg) {
	nlohmann::json root;

	// agents
	auto ja = nlohmann::json::array();
	for (auto const& a : cfg.agents) {
		auto j = nlohmann::json::object();
		j["id"] = a.id;
		j["name"] = a.name;
		j["enabled"] = a.enabled;
		j["platform"] = a.platform;

		// QQ
		{
			auto qq = nlohmann::json::object();
			qq["app_id"] = a.qq_app_id;
			qq["app_secret"] = a.qq_app_secret;
			j["qq"] = std::move(qq);
		}

		j["llm_provider"] = a.llm_provider;

		// DeepSeek
		{
			auto ds = nlohmann::json::object();
			ds["api_key"] = a.deepseek_api_key;
			ds["model"] = a.deepseek_model;
			ds["user_token"] = a.deepseek_user_token;
			ds["waf_cookie"] = a.deepseek_waf_cookie;
			j["deepseek"] = std::move(ds);
		}

		// OpenAI
		{
			auto oa = nlohmann::json::object();
			oa["api_key"] = a.openai_api_key;
			oa["model"] = a.openai_model;
			oa["base_url"] = a.openai_base_url;
			j["openai"] = std::move(oa);
		}

		j["workspace"] = a.workspace;
		j["storage_dir"] = a.storage_dir;

		if (!a.admin_user_ids.empty()) {
			auto adm = nlohmann::json::array();
			for (auto const& id : a.admin_user_ids)
				adm.push_back(id);
			j["admins"] = std::move(adm);
		}

		j["mode"] = a.default_mode == runtime_mode::agent ? "agent" : "plugin";
		j["agent_reach_enabled"] = a.agent_reach_enabled;
		j["max_messages_per_minute"] = a.max_messages_per_minute;
		j["daily_token_budget"] = a.daily_token_budget;

		{
			auto si = nlohmann::json::object();
			si["enabled"] = a.self_iterate_enabled;
			si["interval_hours"] = a.self_iterate_interval_hours;
			si["min_conversations"] = a.self_iterate_min_conversations;
			si["claude_path"] = a.claude_code_path;
			j["self_iteration"] = std::move(si);
		}

		ja.push_back(std::move(j));
	}
	root["agents"] = std::move(ja);

	// global
	{
		auto g = nlohmann::json::object();
		g["verify_tls"] = cfg.global.verify_tls;
		if (!cfg.global.log_file.empty())
			g["log_file"] = cfg.global.log_file;

		auto mgmt = nlohmann::json::object();
		mgmt["enabled"] = cfg.global.management_api_enabled;
		mgmt["listen"] = cfg.global.management_listen;
		mgmt["jwt_secret"] = cfg.global.jwt_secret;
		g["management_api"] = std::move(mgmt);

		root["global"] = std::move(g);
	}

	return root.dump(2);
}

// ─── save_bot_config ───────────────────────────────────────────────────────
// Atomic write: write temp file first, then rename.

void client::save_bot_config(std::string const& path, bot_config const& cfg) {
	auto tmp = path + ".tmp";
	{
		std::ofstream out(tmp);
		if (!out)
			throw std::runtime_error("failed to open temp config for writing: " + tmp);
		out << to_json(cfg);
		if (!out)
			throw std::runtime_error("failed to write temp config: " + tmp);
	}
	std::error_code ec;
	std::filesystem::rename(tmp, path, ec);
	if (ec)
		throw std::runtime_error("failed to rename temp config: " + ec.message());
}
