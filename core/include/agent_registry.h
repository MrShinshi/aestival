/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "agent_instance.h"
#include "bot_config.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace client {

struct plugin_manager;
struct agent_reach_client;
struct chat_storage_backend;

// ─── agent_registry ────────────────────────────────────────────────────────
//
// Central lifecycle manager for all agent instances in the process.
//
// Responsibilities:
//   - create agent_instance from agent_config (factory)
//   - start / stop individual agents
//   - track status + metrics
//   - persist config changes to disk
//   - provide list for management API / system commands
//
// Thread safety: all public methods are guarded by a single mutex.
// Agent start/stop is async (session threads) — status reflects the
// last known state.

struct agent_registry {
	// ── factory ──────────────────────────────────────────────────────────

	// Dependencies shared across all agents (plugins, reach client).
	struct shared_deps {
		plugin_manager& plugins;
		std::shared_ptr<agent_reach_client> reach;
		std::string config_path; // for persist()
	};

	explicit agent_registry(shared_deps deps);

	~agent_registry();

	// Non-copyable, non-movable.
	agent_registry(agent_registry const&) = delete;
	agent_registry& operator=(agent_registry const&) = delete;

	// ── lifecycle ────────────────────────────────────────────────────────

	// Build and start all agents listed in `cfg.agents` where enabled == true.
	// Called once at startup.
	void start_all(bot_config const& cfg);

	// Stop all agents and join their threads.
	void stop_all();

	// ── per-agent operations ─────────────────────────────────────────────

	// Add a new agent at runtime.  Creates session + controller + starts it.
	// Throws std::runtime_error on duplicate id or invalid config.
	void add_agent(agent_config cfg);

	// Remove an agent.  Stops and destroys it.
	// Throws std::runtime_error if not found.
	void remove_agent(std::string_view id);

	// Start a previously stopped (or newly added) agent.
	void start_agent(std::string_view id);

	// Stop (but keep in registry) an agent.
	void stop_agent(std::string_view id);

	// Update an agent's config.  Agent must be stopped first.
	// Throws std::runtime_error if not found or agent is running.
	void update_agent_config(std::string_view id, agent_config cfg);

	// ── queries ──────────────────────────────────────────────────────────

	// List all agents with their current status.
	std::vector<std::pair<std::string, agent_status>> list_agents() const;

	// Get a reference to an agent instance (or nullptr).
	agent_instance* get_agent(std::string_view id);

	// Number of registered agents.
	size_t count() const;

	// ── persistence ──────────────────────────────────────────────────────

	// Save current config (all agents + global) to the config file.
	void persist(global_config const& global);

	// ── callback ─────────────────────────────────────────────────────────
	// Called after startup of each agent completes (both success & failure).
	std::function<void(std::string_view agent_id, bool connected)> on_agent_startup;

	private:
	// Internal: create the platform session for a given config.
	std::unique_ptr<platform_session> create_session(agent_config const& cfg);

	// Internal: build and wire a full agent_instance.
	void build_agent(agent_instance& inst);

	// Internal: start the session and begin message processing.
	void launch_agent(agent_instance& inst);

	shared_deps deps_;
	std::unordered_map<std::string, std::unique_ptr<agent_instance>> agents_;
	mutable std::mutex mutex_;
};

} // namespace client
