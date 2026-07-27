/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "tool_registry.h"
#include "bot_config.h" // mcp_server_config

#include <nlohmann/json_fwd.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace client {

// ─── mcp_client ───────────────────────────────────────────────────────────
// MCP (Model Context Protocol) client over stdio transport.
//
// Launches a subprocess running an MCP server, performs the JSON-RPC 2.0
// handshake, discovers tools via tools/list, and exposes them as a standard
// tool_provider so the existing tool_registry / agent_controller can use
// them without modification.
//
// Thread-safety: all I/O is guarded by io_mutex_.  execute_tool() may be
// called from std::async (parallel tool calls); the mutex serialises pipe
// access so responses never interleave.

class mcp_client : public tool_provider {
	public:
	explicit mcp_client(mcp_server_config cfg);
	~mcp_client() override;

	// ── tool_provider interface ──────────────────────────────────────────
	std::vector<tool_definition> get_tools() const override;
	std::string execute_tool(std::string_view tool_name, nlohmann::json const& args) override;

	// ── health ───────────────────────────────────────────────────────────
	bool connected() const {
		return connected_.load();
	}
	std::string const& server_name() const {
		return server_name_;
	}
	size_t tool_count() const {
		return tools_.size();
	}

	private:
	// ── lifecycle ────────────────────────────────────────────────────────
	bool launch();
	bool perform_handshake();
	void terminate();

	// ── JSON-RPC 2.0 framing ─────────────────────────────────────────────
	nlohmann::json send_request(std::string_view method, nlohmann::json const& params);
	void send_notification(std::string_view method, nlohmann::json const& params);
	std::string read_line();
	void write_line(nlohmann::json const& j);

	// ── helpers ──────────────────────────────────────────────────────────
	static tool_definition convert_tool(nlohmann::json const& mcp_tool);
	static std::string extract_content(nlohmann::json const& result);
	std::string read_response(int64_t expected_id);

	mcp_server_config cfg_;
	std::string server_name_;
	std::string server_version_;
	std::vector<tool_definition> tools_;

	std::atomic<bool> connected_{false};
	int64_t next_id_ = 1;
	mutable std::mutex io_mutex_;

	// ── subprocess handles ───────────────────────────────────────────────
#ifdef _WIN32
	HANDLE h_process_ = nullptr;
	HANDLE h_thread_ = nullptr;
	HANDLE h_stdin_wr_ = nullptr;	// we write → child reads
	HANDLE h_stdout_rd_ = nullptr;	// child writes → we read
#else
	pid_t child_pid_ = -1;
	int stdin_fd_ = -1;
	int stdout_fd_ = -1;
#endif
};

} // namespace client
