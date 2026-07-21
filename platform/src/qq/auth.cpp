/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "platform/qq/session.h"
#include "platform/http_client.h"
#include "platform/log.h"

namespace platform::qq {
namespace {

struct token_response {
	std::string access_token;
	int expires_in = 0;
};

void from_json(nlohmann::json const& j, token_response& v) {
	v.access_token = j.value("access_token", "");
	if (j.contains("expires_in")) {
		try {
			if (j["expires_in"].is_number_integer())
				v.expires_in = j["expires_in"].get<int>();
			else if (j["expires_in"].is_string()) {
				try {
					v.expires_in = std::stoi(j["expires_in"].get<std::string>());
				} catch (...) {
					v.expires_in = 0;
				}
			}
		} catch (...) {
			v.expires_in = 0;
		}
	}
}

struct gateway_response {
	std::string url;
};
void from_json(nlohmann::json const& j, gateway_response& v) {
	v.url = j.value("url", "");
}

} // namespace

// ─── http_request_async ───────────────────────────────────────────────────

boost::asio::awaitable<std::pair<int, std::string>>
session::http_request_async(std::string const& method, std::string const& host, std::string const& path,
							std::string body, std::map<std::string, std::string> const& headers) {
	co_return co_await platform::detail::http_request_async(method, host, path, std::move(body), headers, verify_tls_);
}

// ─── login_async ──────────────────────────────────────────────────────────

boost::asio::awaitable<bool> session::login_async() {
	std::string body = R"({"appId":")" + app_id() + R"(","clientSecret":")" + app_secret() + "\"}";

	auto [status, response] =
		co_await http_request_async("POST", "bots.qq.com", "/app/getAppAccessToken", std::move(body),
									{{"Content-Type", "application/json"}, {"Accept", "application/json"}});

	if (status != 200) {
		std::ostringstream s;
		s << "[asio] token failed: " << status << " body: " << response;
		log::error(s.str());
		co_return false;
	}

	try {
		auto t = nlohmann::json::parse(response).get<token_response>();
		if (t.access_token.empty()) {
			co_return false;
		}

		int refresh_after_sec = 3600;
		set_access_token(std::move(t.access_token));
		if (t.expires_in > 0) {
			access_token_expires_at_ = std::chrono::system_clock::now() + std::chrono::seconds(t.expires_in);
			refresh_after_sec = std::max(0, t.expires_in - 60);
		}

		int gen = token_refresh_generation_.fetch_add(1) + 1;
		// boost::asio::basic_waitable_timer::cancel() no longer accepts an error_code parameter
		// in newer Boost versions; call cancel() without args.
		token_refresh_timer_.cancel();
		token_refresh_timer_.expires_after(std::chrono::seconds(refresh_after_sec));

		std::ostringstream s;
		s << "[asio] token ok, refresh in " << refresh_after_sec << "s (gen=" << gen << ")";
		log::info(s.str());

		boost::asio::co_spawn(ioc_, token_refresh_timer_loop(gen), boost::asio::detached);

		co_return true;
	} catch (...) {
	}

	co_return false;
}

// ─── fetch_gateway_async ──────────────────────────────────────────────────

boost::asio::awaitable<bool> session::fetch_gateway_async() {
	std::string token = access_token();

	auto [status, response] = co_await http_request_async("GET", "api.sgroup.qq.com", "/gateway/bot", "",
														  {{"Authorization", "QQBot " + token}});

	if (status != 200) {
		std::ostringstream s;
		s << "[asio] gateway failed: " << status;
		log::error(s.str());
		co_return false;
	}

	try {
		auto gw = nlohmann::json::parse(response).get<gateway_response>();
		if (!gw.url.empty()) {
			set_gateway_url(std::move(gw.url));
			std::ostringstream s;
			s << "[asio] gateway ok: " << gateway_url();
			log::info(s.str());
			co_return true;
		}
	} catch (...) {
	}

	co_return false;
}

// ─── token_refresh_timer_loop ─────────────────────────────────────────────

boost::asio::awaitable<void> session::token_refresh_timer_loop(int gen) {
	boost::beast::error_code ec;
	co_await token_refresh_timer_.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));
	if (!ec && running_ && token_refresh_generation_.load() == gen)
		co_await login_async();
}
} // namespace platform::qq
