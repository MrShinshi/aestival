/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "bot_messaging.h"
#include "encode_utils.h"
#include "message_types.h"
#include "tool_registry.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace client {

enum struct plugin_capability : std::uint32_t { none = 0, send_message = 1u << 0, request_stop = 1u << 1 };

inline plugin_capability operator|(plugin_capability lhs, plugin_capability rhs) {
	return static_cast<plugin_capability>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

inline bool has_capability(plugin_capability value, plugin_capability flag) {
	return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

struct message_receipt {
	bool attempted = false;
	bool delivered = false;
	std::string detail;
};

struct plugin_result {
	bool handled = false;
	bool stop_processing = false;
};

struct plugin_context {
	static constexpr std::size_t max_reply_length = 4096;

	plugin_context(bot_messaging& bot, const message_event& message) : bot_(bot), message_(message) {
	}

	const message_event& message() const {
		return message_;
	}
	std::string_view content() const {
		return message_.content;
	}

	bool reply(std::string_view content) {
		last_receipt_ = {};
		last_receipt_.attempted = true;

		std::string safe_content = sanitize_reply(content);
		if (safe_content.empty()) {
			last_receipt_.detail = "empty reply blocked";
			return false;
		}

		bool sent = false;
		if (message_.is_guild && message_.is_private && !message_.guild_id.empty()) {
			sent = bot_.send_dms_md(message_.guild_id, safe_content);
		} else if (message_.is_guild && !message_.channel_id.empty()) {
			sent = bot_.send_channel_md(message_.channel_id, safe_content);
		} else if (message_.is_group) {
			sent = bot_.send_group_md(message_.group_id, safe_content);
		} else {
			sent = bot_.send_private_md(message_.user_openid, safe_content);
		}

		last_receipt_.delivered = sent;
		last_receipt_.detail = sent ? "sent (md)" : "send failed";
		return sent;
	}

	void request_stop() {
		stop_requested_ = true;
	}
	bool stop_requested() const {
		return stop_requested_;
	}
	const message_receipt& last_receipt() const {
		return last_receipt_;
	}

	private:
	static std::string sanitize_reply(std::string_view content) {
		std::string result;
		result.reserve(content.size() < max_reply_length ? content.size() : max_reply_length);
		for (char ch : content) {
			if (ch == '\0')
				continue;
			result.push_back(ch);
			if (result.size() >= max_reply_length)
				break;
		}
		return sanitize_utf8(result);
	}

	bot_messaging& bot_;
	const message_event& message_;
	bool stop_requested_ = false;
	message_receipt last_receipt_{};
};

// ─── plugin (now also a tool_provider) ─────────────────────────────
// P2-2: Every plugin can optionally expose tools for the agent.

struct plugin : tool_provider {
	virtual ~plugin() = default;
	virtual std::string_view name() const = 0;
	virtual int priority() const {
		return 0;
	}
	virtual plugin_capability capabilities() const {
		return plugin_capability::send_message;
	}
	virtual bool can_handle(const message_event& message) const = 0;
	virtual plugin_result handle(plugin_context& context) = 0;

	// ── tool_provider overrides ────────────────────────────────────
	std::vector<tool_definition> get_tools() const override {
		return {};
	}
	std::string execute_tool(std::string_view /*name*/, nlohmann::json const& /*args*/) override {
		return "not implemented";
	}
};

} // namespace client
