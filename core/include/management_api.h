/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "management_auth.h"

#include <boost/asio.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace client {

struct agent_registry;
struct global_config;

// ─── management_api ────────────────────────────────────────────────────────
//
// Lightweight HTTP server that exposes a REST API for the Web UI.
// Runs on a dedicated thread with its own io_context.
//
// Endpoints:
//   GET  /api/v1/health
//   GET  /api/v1/agents
//   POST /api/v1/agents
//   DELETE /api/v1/agents/:id
//   POST /api/v1/agents/:id/start
//   POST /api/v1/agents/:id/stop
//   PUT  /api/v1/agents/:id/config
//   GET  /api/v1/logs?level=...&limit=...
//   GET  /api/v1/conversations?agent_id=...
//   GET  /api/v1/conversations/:id
//
// All endpoints except /health require a Bearer token in the
// Authorization header.

struct management_api {
	// Create and start the server.
	// `listen_addr` format: "127.0.0.1" (port from global_config)
	// Call start() to launch the thread.
	management_api(agent_registry& registry, global_config const& global);

	~management_api();

	// Non-copyable.
	management_api(management_api const&) = delete;
	management_api& operator=(management_api const&) = delete;

	// Start the HTTP server thread.
	void start();

	// Stop the server and join the thread.
	void stop();

	// Whether the server is currently running.
	bool is_running() const;

	private:
	struct impl;
	std::unique_ptr<impl> impl_;
};

} // namespace client
