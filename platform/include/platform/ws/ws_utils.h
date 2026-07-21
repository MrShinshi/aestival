/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <string>
#include <boost/beast/core/error.hpp>

namespace platform::ws::detail {

bool parse_url(const std::string& url, std::string& host, std::string& path, int& port, int default_port);
bool is_expected_shutdown_error(const boost::beast::error_code& ec);
std::string make_sni_host(const std::string& host);

} // namespace platform::ws::detail
