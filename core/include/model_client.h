/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "chat_context_manager.h"

#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace client {

// ─── model_response ─────────────────────────────────────────────────────
// Generic response from any LLM provider.  Mirrors deepseek_response so
// existing code works unchanged.

struct model_response {
	std::string content;
	std::vector<tool_call> tool_calls;
	nlohmann::json usage; // raw "usage" object from API response
};

// ─── model_client (abstract interface) ──────────────────────────────────
// All LLM providers implement this.  agent_controller and
// chat_context_manager depend on the interface, not concrete types.

struct model_client {
	virtual ~model_client() = default;

	// Quick check: is this provider configured with a valid key?
	virtual bool is_enabled() const = 0;

	// Simple single-prompt completion (used by summarization).
	virtual std::string complete(std::string_view prompt) const = 0;

	// Multi-message conversation completion (no tools).
	virtual std::string complete_messages(std::vector<chat_message> const& messages) const = 0;

	// Completion with tool definitions (function calling).
	virtual model_response complete_with_tools(std::vector<chat_message> const& messages,
											   nlohmann::json const& tools) const = 0;

	// Human-readable model identifier for logs / stats.
	virtual std::string model_name() const = 0;

	// Optional: query account balance (throws if unsupported).
	virtual std::string query_balance() const {
		return "{}";
	}

	// Optional: query usage stats from the provider's dashboard API.
	// Returns raw JSON from the platform (or "{}" if unsupported).
	virtual std::string query_usage_json(int /*year*/, int /*month*/) const {
		return "{}";
	}

	// Optional: provider name for config display.
	virtual std::string provider_name() const = 0;
};

} // namespace client
