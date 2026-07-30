/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "plugin.h"

#include <string>
#include <string_view>

namespace client::plugins {

struct simple_test_plugin final : public client::plugin {
	std::string_view name() const override {
		return "simple_test";
	}

	plugin_descriptor descriptor() const override {
		plugin_descriptor d;
		d.name = "simple_test";
		d.display_name = "Simple Test";
		d.description = "Responds to hello/ping with a basic greeting.";
		d.version = "1.0";
		d.default_enabled = true;
		return d;
	}

	int priority() const override {
		return -100;
	}

	plugin_capability capabilities() const override {
		return plugin_capability::send_message;
	}

	bool can_handle(const message_event& message) const override {
		auto const& c = message.content;
		return c == "hello" || c == "你好" || c == "ping";
	}

	plugin_result handle(plugin_context& context) override {
		std::string const& content = context.message().content;

		if (content == "hello" || content == "你好") {
			context.reply("Hello! I'm 绯英, an AI-powered QQ Bot.");
			return {true, true};
		}
		if (content == "ping") {
			context.reply("pong!");
			return {true, true};
		}

		return {false, false};
	}
};

} // namespace client::plugins
