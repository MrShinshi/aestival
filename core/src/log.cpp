/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "log.h"

std::mutex client::log::mutex_;

namespace {

std::ofstream log_file_;

} // namespace

namespace {

std::string to_level_text(client::log::level value) {
	auto name = magic_enum::enum_name(value);
	if (name.empty()) {
		return "UNKNOWN";
	}
	std::string result{name};
	for (auto& ch : result) {
		ch = static_cast<char>(::toupper(static_cast<unsigned char>(ch)));
	}
	return result;
}

std::string make_timestamp() {
	auto now = std::chrono::system_clock::now();
	auto time = std::chrono::system_clock::to_time_t(now);
	std::tm local_time{};
#ifdef _WIN32
	localtime_s(&local_time, &time);
#else
	localtime_r(&time, &local_time);
#endif

	std::ostringstream stream;
	stream << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
	return stream.str();
}

} // namespace

void client::log::debug(std::string_view message) {
	write(level::debug, message);
}

void client::log::info(std::string_view message) {
	write(level::info, message);
}

void client::log::warn(std::string_view message) {
	write(level::warn, message);
}

void client::log::error(std::string_view message) {
	write(level::error, message);
}

void client::log::init(std::string_view log_path) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (log_file_.is_open())
		log_file_.close();
	if (!log_path.empty()) {
		log_file_.open(std::string(log_path), std::ios::app);
	}
}

void client::log::write(level level, std::string_view message) {
	std::lock_guard<std::mutex> lock(mutex_);
	std::ostream& stream = std::cerr;
	auto const line = std::string("[") + make_timestamp() + "] [" + to_level_text(level) + "] [tid=" + ([&] {
						  std::ostringstream s;
						  s << std::this_thread::get_id();
						  return s.str();
					  }()) +
					  "] " + std::string(message);
	stream << line << std::endl;
	if (log_file_.is_open())
		log_file_ << line << std::endl;
}
