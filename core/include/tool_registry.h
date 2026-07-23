/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace client {

// ─── tool_definition ─────────────────────────────────────────────────────
// A single tool that an agent can invoke via function calling.

struct tool_definition {
	std::string name;
	std::string description;
	nlohmann::json parameters; // JSON Schema for the tool's arguments
};

// ─── tool_provider ───────────────────────────────────────────────────────
// Interface for anything that can expose tools to the agent.
// Plugins implement this to register their capabilities.

struct tool_provider {
	virtual ~tool_provider() = default;

	// Return the list of tools this provider exposes.
	// Empty by default — override in plugins that provide tools.
	virtual std::vector<tool_definition> get_tools() const {
		return {};
	}

	// Execute a tool call.  `tool_name` from get_tools(), `args` is the
	// JSON from the LLM's function call.
	// Returns the tool output as a string.
	virtual std::string execute_tool(std::string_view tool_name, nlohmann::json const& /*args*/) {
		return "Error: tool '" + std::string(tool_name) + "' not implemented";
	}
};

// ─── tool_registry ───────────────────────────────────────────────────────
// Central registry that collects tools from all registered providers and
// builds the tools JSON array for the LLM API.

struct tool_registry {
	// Register a tool provider (typically a plugin).
	void register_provider(std::shared_ptr<tool_provider> provider) {
		if (!provider)
			return;
		providers_.push_back(std::move(provider));
	}

	// Build the full "tools" JSON array for the chat completions API.
	nlohmann::json build_tools_json() const {
		auto arr = nlohmann::json::array();
		for (auto const& p : providers_) {
			for (auto const& td : p->get_tools()) {
				auto fn = nlohmann::json::object();
				fn["name"] = td.name;
				fn["description"] = td.description;
				fn["parameters"] = td.parameters;

				auto tool = nlohmann::json::object();
				tool["type"] = "function";
				tool["function"] = std::move(fn);
				arr.push_back(std::move(tool));
			}
		}
		return arr;
	}

	// Find and execute a tool by name.  Returns the result string.
	std::string execute(std::string_view tool_name, nlohmann::json const& args) const {
		for (auto const& p : providers_) {
			for (auto const& td : p->get_tools()) {
				if (td.name == tool_name)
					return p->execute_tool(tool_name, args);
			}
		}
		return "Error: unknown tool '" + std::string(tool_name) + "'";
	}

	// Check if any provider has registered tools.
	bool empty() const {
		return providers_.empty();
	}

	private:
	std::vector<std::shared_ptr<tool_provider>> providers_;
};

} // namespace client
