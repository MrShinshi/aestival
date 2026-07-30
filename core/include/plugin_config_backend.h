/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace client {

// ─── plugin_config_backend ───────────────────────────────────────────────
// Abstract interface for persisting per-agent and per-user plugin state.
// Follows the same pattern as chat_storage_backend — swap implementations
// without touching business logic.

struct plugin_config_backend {
	virtual ~plugin_config_backend() = default;

	// ── Agent-level ────────────────────────────────────────────────────
	virtual bool is_plugin_enabled_for_agent(std::string const& agent_id,
											 std::string const& plugin_name) = 0;
	virtual void set_plugin_enabled_for_agent(std::string const& agent_id,
											  std::string const& plugin_name, bool enabled) = 0;
	virtual nlohmann::json get_plugin_config_for_agent(std::string const& agent_id,
													   std::string const& plugin_name) = 0;
	virtual void set_plugin_config_for_agent(std::string const& agent_id,
											 std::string const& plugin_name,
											 nlohmann::json const& config) = 0;

	// ── User-level ─────────────────────────────────────────────────────
	virtual bool is_plugin_enabled_for_user(std::string const& user_id,
											std::string const& plugin_name) = 0;
	virtual void set_plugin_enabled_for_user(std::string const& user_id,
											 std::string const& plugin_name, bool enabled) = 0;

	// ── Batch queries (for management UIs) ─────────────────────────────
	// Returns: [(plugin_name, enabled, config_json_string), ...]
	virtual std::vector<std::tuple<std::string, bool, std::string>>
	list_agent_plugins(std::string const& agent_id) = 0;
};

} // namespace client
