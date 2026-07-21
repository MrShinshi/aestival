/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "platform/qq/api.h"
#include "platform/log.h"

namespace platform::qq {

api::api(session& s) : s_(s) {
}

void api::stop() {
	s_.stop();
}

std::string api::get_token() const {
	return s_.access_token();
}

boost::asio::awaitable<void> api::execute(std::string method, std::string path, std::string body,
										  std::map<std::string, std::string> headers, const char* log_prefix,
										  api_callback cb) {
	try {
		auto [status, resp] =
			co_await s_.http_request_async(method, "api.sgroup.qq.com", path, std::move(body), headers);
		std::ostringstream ss;
		ss << log_prefix << " HTTP " << status;
		log::info(ss.str());
		if (cb)
			cb(status, std::move(resp));
	} catch (std::exception const& e) {
		std::ostringstream ss;
		ss << log_prefix << " failed: " << e.what();
		log::error(ss.str());
		if (cb)
			cb(-1, "");
	}
}

} // namespace platform::qq
