/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <log.h> // resolves to core/include/log.h via include-path, not relative to this file
#include <string>
#include <string_view>

namespace platform {

// Thin forwarding wrapper: delegates to client::log so all layers share one
// logging backend (file + console via core::log).
struct log {
	static void init(std::string const& path) {
		client::log::init(path);
	}
	static void info(std::string_view m) {
		client::log::info(m);
	}
	static void warn(std::string_view m) {
		client::log::warn(m);
	}
	static void error(std::string_view m) {
		client::log::error(m);
	}
	static void debug(std::string_view m) {
		client::log::debug(m);
	}
};

} // namespace platform
