/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "platform/ws/ws_utils.h"

namespace ssl = boost::asio::ssl;
namespace websocket = boost::beast::websocket;

namespace platform::ws::detail {

bool parse_url(std::string const& url, std::string& host, std::string& path, int& port, int default_port) {
	boost::regex re(R"(^(wss?)://(\[[^\]]+\]|[^:/]+)(?::(\d+))?(.*)$)", boost::regex::icase);
	boost::smatch match;
	if (!boost::regex_match(url, match, re)) {
		return false;
	}

	host = match[2].str();

	if (match[3].matched) {
		try {
			port = std::stoi(match[3].str());
		} catch (...) {
			return false;
		}
	} else {
		port = default_port;
	}

	path = match[4].str();
	if (path.empty()) {
		path = "/";
	}
	return true;
}

bool is_expected_shutdown_error(boost::beast::error_code const& ec) {
	return ec == boost::asio::error::eof || ec == ssl::error::stream_truncated || ec == websocket::error::closed;
}

std::string make_sni_host(std::string const& host) {
	if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
		return host.substr(1, host.size() - 2);
	}

	return host;
}

} // namespace platform::ws::detail
