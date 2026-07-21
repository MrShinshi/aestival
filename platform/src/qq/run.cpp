/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "platform/qq/session.h"
#include "platform/log.h"

namespace platform::qq {

boost::asio::awaitable<void> session::run() {
	log::info("[asio] starting...");

	while (running_) {
		log::info("[asio] 1. getting token...");
		if (!co_await login_async()) {
			boost::asio::steady_timer retry_timer(ioc_);
			retry_timer.expires_after(std::chrono::seconds(login_retry_delay_s));
			boost::beast::error_code ec;
			co_await retry_timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));
			if (ec || !running_) {
				break;
			}
			continue;
		}

		log::info("[asio] 2. fetching gateway...");
		if (!co_await fetch_gateway_async()) {
			boost::asio::steady_timer retry_timer(ioc_);
			retry_timer.expires_after(std::chrono::seconds(login_retry_delay_s));
			boost::beast::error_code ec;
			co_await retry_timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));
			if (ec || !running_) {
				break;
			}
			continue;
		}

		co_await run_websocket_async();
		if (!running_) {
			break;
		}

		std::ostringstream s;
		s << "[asio] disconnected, reconnecting in " << reconnect_delay_s << "s...";
		log::warn(s.str());
		boost::asio::steady_timer reconnect_timer(ioc_);
		reconnect_timer.expires_after(std::chrono::seconds(reconnect_delay_s));
		boost::beast::error_code ec;
		co_await reconnect_timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));
		if (ec || !running_) {
			break;
		}
	}
}

} // namespace platform::qq
