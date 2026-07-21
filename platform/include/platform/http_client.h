/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <string>
#include <map>
#include <utility>
#include <boost/asio/awaitable.hpp>

namespace platform {
namespace detail {

using boost::asio::awaitable;

awaitable<std::pair<int, std::string>> http_request_async(const std::string& method, const std::string& host,
														  const std::string& path, std::string body,
														  const std::map<std::string, std::string>& headers,
														  bool verify_tls = true);

} // namespace detail
} // namespace platform
