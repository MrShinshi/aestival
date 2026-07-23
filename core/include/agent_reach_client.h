/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace client {

// ─── capability flags ───────────────────────────────────────────────────
struct agent_reach_capabilities {
	bool exa_search = false; // Exa via mcporter (needs Node.js)
	bool web_fetch = false;	 // Jina Reader via curl (zero config)
	bool v2ex = false;		 // V2EX public API via curl (zero config)
	bool bilibili = false;	 // bili-cli
	bool github = false;	 // gh CLI
	bool twitter = false;	 // twitter-cli

	bool any_search = false; // at least one search backend is ready

	// For debug/logging.
	std::string summary() const;
};

// ─── agent_reach_client ─────────────────────────────────────────────────
//
// Wraps CLI tools from the agent-reach ecosystem.  All calls are
// synchronous — the caller is already on its own per-convo thread.
//
// Backend → agent-reach CLI command (matching Claude Code's own usage):
//   Exa  → mcporter call 'exa.web_search_exa(query:"...",numResults:N)' --output json
//   Web  → curl -s "https://r.jina.ai/URL" --max-time 15
//   B站  → bili search "query" --type video -n N
//   V2EX → curl -s "https://www.v2ex.com/api/topics/hot.json"
//   GitHub → gh search repos "query" --sort stars --limit N
//   Twitter → twitter search "query" -n N

struct agent_reach_client {
	explicit agent_reach_client(bool verify_tls = true);

	// Probe available backends (checks PATH, cheap).
	agent_reach_capabilities probe();
	agent_reach_capabilities const& capabilities() const {
		return caps_;
	}

	// ── single-platform tools ──────────────────────────────────────────
	std::string search_exa(std::string_view query, int num_results = 5);
	std::string fetch_page(std::string_view url);
	std::string search_bilibili(std::string_view query, int max_results = 5);
	std::string search_v2ex(std::string_view query);
	std::string search_github(std::string_view query, int max_results = 5);
	std::string search_twitter(std::string_view query, int max_results = 5);

	// ── hot/trending ──────────────────────────────────────────────────
	std::string bili_hot(int n = 10);

	// ── zero-config fallback search ───────────────────────────────────
	// Wikipedia API: no key, works everywhere, structured JSON.
	std::string search_wikipedia(std::string_view query, int num_results = 5);

	// Low-level: execute a shell command and return stdout.
	static std::string exec(std::string_view cmd, std::chrono::seconds timeout = std::chrono::seconds(30));

	private:
[[maybe_unused]] 	bool verify_tls_;

	static bool command_available(std::string_view name);

	agent_reach_capabilities caps_;
	bool probed_ = false;
};

} // namespace client
