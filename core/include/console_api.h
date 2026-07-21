/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "bot_messaging.h"

#include <iostream>

namespace client {

// Minimal console-backed messaging for testing without QQ network.
// Prints all output to std::cout with `[bot]` prefix.
struct console_api : bot_messaging {
	void stop() override {
		stopped_ = true;
	}
	bool is_stopped() const {
		return stopped_;
	}

	bool send_private_message(std::string_view, std::string_view c) override {
		// Write to stderr — pipe-capturable on every platform
		std::cerr << "[bot] " << c << "\n\n";
		return true;
	}
	bool send_private_md(std::string_view, std::string_view c) override {
		return send_private_message({}, c);
	}
	bool send_group_md(std::string_view, std::string_view c) override {
		return send_private_message({}, c);
	}
	bool send_channel_md(std::string_view, std::string_view c) override {
		return send_private_message({}, c);
	}
	bool send_dms_md(std::string_view, std::string_view c) override {
		return send_private_message({}, c);
	}

	private:
	bool stopped_ = false;
};

} // namespace client
