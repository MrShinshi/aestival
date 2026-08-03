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

	int login_attempts = 0;
	int reconnect_attempts = 0;

	while (running_) {
		log::info("[asio] 1. getting token...");
		if (!co_await login_async()) {
			++login_attempts;
			int delay = std::min(login_retry_delay_s * (1 << std::min(login_attempts - 1, 3)), 60);
			log::warn("[asio] login failed, attempt " + std::to_string(login_attempts) +
			          " — retrying in " + std::to_string(delay) + "s");
			boost::asio::steady_timer retry_timer(ioc_);
			retry_timer.expires_after(std::chrono::seconds(delay));
			boost::beast::error_code ec;
			co_await retry_timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));
			if (ec || !running_) {
				break;
			}
			continue;
		}
		login_attempts = 0;

		log::info("[asio] 2. fetching gateway...");
		if (!co_await fetch_gateway_async()) {
			int delay = login_retry_delay_s;
			boost::asio::steady_timer retry_timer(ioc_);
			retry_timer.expires_after(std::chrono::seconds(delay));
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

		// Exponential backoff for reconnects: 3s → 6s → 12s → ... → 60s max.
		++reconnect_attempts;
		int delay = std::min(reconnect_delay_s * (1 << std::min(reconnect_attempts - 1, 4)), 60);
		std::ostringstream s;
		s << "[asio] disconnected, reconnecting in " << delay << "s (attempt " << reconnect_attempts << ")...";
		log::warn(s.str());
		boost::asio::steady_timer reconnect_timer(ioc_);
		reconnect_timer.expires_after(std::chrono::seconds(delay));
		boost::beast::error_code ec;
		co_await reconnect_timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));
		if (ec || !running_) {
			break;
		}
	}
	// Reset reconnect counter on clean exit
	reconnect_attempts = 0;
}

} // namespace platform::qq
