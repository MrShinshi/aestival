/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <mutex>
#include <string_view>

namespace client {

struct log {
	enum struct level { debug, info, warn, error };

	static void debug(std::string_view message);
	static void info(std::string_view message);
	static void warn(std::string_view message);
	static void error(std::string_view message);

	// Open a log file.  Messages are still printed to console; the file
	// receives the same lines.  Pass an empty path to disable file logging.
	static void init(std::string_view log_path);

	private:
	static void write(level level, std::string_view message);
	static std::mutex mutex_;
};

} // namespace client
