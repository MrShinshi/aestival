/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace client {

// ─── agent_reach_client ──────────────────────────────────────────────────
//
// Minimal utility: synchronous subprocess execution and PATH probing.
//
// All search/fetch tools have moved to MCP servers (mcporter, agent-reach, etc.).
// This class now only serves self_iteration_engine which needs shell
// access for `claude -p` and `git` commands.

struct agent_reach_client {
	// Execute a shell command synchronously, return stdout as string.
	// On timeout, the child is killed and "[TIMEOUT]" is appended.
	static std::string exec(std::string_view cmd, std::chrono::seconds timeout = std::chrono::seconds(30));

	// Check whether a command is on PATH (or in known install locations).
	static bool command_available(std::string_view name);
};

} // namespace client
