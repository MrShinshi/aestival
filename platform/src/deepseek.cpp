/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "platform/deepseek.h"
#include "platform/detail/http_utils.h"
#include "platform/detail/sanitize.h"
#include "platform/log.h"

#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <openssl/ssl.h>
#include <stdexcept>
#include <chrono>
#include <sstream>

namespace platform::deepseek {
namespace {
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

static raw_chat_response send_chat_request(std::string_view api_key, std::string_view model,
										   nlohmann::json const& messages, nlohmann::json const* tools,
										   bool verify_tls) {
	boost::asio::io_context ioc;
	ssl::context ssl_ctx = platform::detail::make_ssl_ctx(verify_tls);
	tcp::resolver resolver(ioc);
	beast::ssl_stream<beast::tcp_stream> stream(ioc, ssl_ctx);

	std::string const host = "api.deepseek.com";
	if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
		throw std::runtime_error("failed to set TLS host name for DeepSeek");

	auto const results = resolver.resolve(host, "443");
	beast::get_lowest_layer(stream).connect(results);
	stream.handshake(ssl::stream_base::client);

	nlohmann::json body = {{"model", std::string(model)}, {"messages", messages}, {"stream", false}};
	if (tools && tools->is_array() && !tools->empty())
		body["tools"] = *tools;

	http::request<http::string_body> req{http::verb::post, "/chat/completions", 11};
	req.set(http::field::host, host);
	req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
	req.set(http::field::authorization, "Bearer " + std::string(api_key));
	req.set(http::field::content_type, "application/json");
	req.set(http::field::accept, "application/json");
	try {
		req.body() = body.dump();
	} catch (nlohmann::json::exception const&) {
		for (auto& m : body["messages"])
			m["content"] = platform::detail::sanitize_utf8(m["content"].get<std::string>());
		req.body() = body.dump();
	}
	req.prepare_payload();
	http::write(stream, req);

	beast::flat_buffer buf;
	http::response<http::string_body> res;
	http::read(stream, buf, res);

	beast::error_code ec;
	stream.shutdown(ec);
	if (ec == boost::asio::error::eof || ec == ssl::error::stream_truncated)
		ec = {};
	if (ec)
		throw beast::system_error(ec);

	if (res.result() != http::status::ok)
		throw std::runtime_error("DeepSeek HTTP " + std::to_string(static_cast<int>(res.result())) + ": " + res.body());

	auto root = nlohmann::json::parse(platform::detail::sanitize_utf8(res.body()), nullptr, false);
	auto const choices = root.find("choices");
	if (choices == root.end() || !choices->is_array() || choices->empty())
		throw std::runtime_error("DeepSeek response missing choices");

	auto const& first = (*choices)[0];
	auto const message = first.find("message");
	if (message == first.end() || !message->is_object())
		throw std::runtime_error("DeepSeek response missing message");

	raw_chat_response result;
	result.content = message->value("content", "");
	auto tcs = message->find("tool_calls");
	if (tcs != message->end() && tcs->is_array())
		result.tool_calls = *tcs;
	auto usage = root.find("usage");
	if (usage != root.end() && usage->is_object())
		result.usage = *usage;
	return result;
}
} // namespace

raw_chat_response chat(nlohmann::json const& messages, nlohmann::json const* tools, std::string_view api_key,
					   std::string_view model, bool verify_tls) {
	return send_chat_request(api_key, model, messages, tools, verify_tls);
}

std::string query_balance(std::string_view api_key, bool verify_tls) {
	boost::asio::io_context ioc;
	ssl::context ssl_ctx = platform::detail::make_ssl_ctx(verify_tls);
	tcp::resolver resolver(ioc);
	beast::ssl_stream<beast::tcp_stream> stream(ioc, ssl_ctx);

	std::string const host = "api.deepseek.com";
	if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
		throw std::runtime_error("failed to set TLS host name");

	auto const results = resolver.resolve(host, "443");
	beast::get_lowest_layer(stream).connect(results);
	stream.handshake(ssl::stream_base::client);

	http::request<http::empty_body> req{http::verb::get, "/user/balance", 11};
	req.set(http::field::host, host);
	req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
	req.set(http::field::authorization, "Bearer " + std::string(api_key));
	http::write(stream, req);

	beast::flat_buffer buf;
	http::response<http::string_body> res;
	http::read(stream, buf, res);

	beast::error_code ec;
	stream.shutdown(ec);
	if (ec == boost::asio::error::eof || ec == ssl::error::stream_truncated)
		ec = {};
	if (ec)
		throw beast::system_error(ec);

	if (res.result() != http::status::ok)
		throw std::runtime_error("balance HTTP " + std::to_string(static_cast<int>(res.result())) + ": " + res.body());
	return res.body();
}

std::string make_time_string() {
	return platform::detail::make_time_string();
}

// ═══════════════════════════════════════════════════════════════════════════
//  DeepSeek Platform web API (usage dashboard — browser userToken auth)
// ═══════════════════════════════════════════════════════════════════════════

namespace {

static nlohmann::json web_api_get(std::string_view host, std::string_view path, std::string_view user_token,
								  std::string_view waf_cookie, bool verify_tls) {
	boost::asio::io_context ioc;
	ssl::context ssl_ctx = platform::detail::make_ssl_ctx(verify_tls);
	tcp::resolver resolver(ioc);
	beast::ssl_stream<beast::tcp_stream> stream(ioc, ssl_ctx);

	if (!SSL_set_tlsext_host_name(stream.native_handle(), std::string(host).c_str()))
		throw std::runtime_error("failed to set TLS host name for platform.deepseek.com");

	auto const results = resolver.resolve(host, "443");
	beast::get_lowest_layer(stream).connect(results);
	stream.handshake(ssl::stream_base::client);

	http::request<http::empty_body> req{http::verb::get, std::string(path), 11};
	req.set(http::field::host, std::string(host));
	req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
	req.set(http::field::authorization, "Bearer " + std::string(user_token));
	req.set(http::field::cookie, std::string(waf_cookie));
	req.set(http::field::accept, "application/json");
	req.set("x-client-bundle-id", "com.deepseek.chat");
	req.set("x-client-locale", "zh_CN");
	req.set("x-client-platform", "web");
	req.set("x-client-timezone-offset", "28800");
	req.set("x-client-version", "1.0.0");
	req.set("referer", "https://platform.deepseek.com/usage");

	http::write(stream, req);

	beast::flat_buffer buf;
	http::response<http::string_body> res;
	http::read(stream, buf, res);

	beast::error_code ec;
	stream.shutdown(ec);
	if (ec == boost::asio::error::eof || ec == ssl::error::stream_truncated)
		ec = {};
	if (ec)
		throw beast::system_error(ec);

	if (res.result() != http::status::ok)
		throw std::runtime_error("DeepSeek Platform HTTP " + std::to_string(static_cast<int>(res.result())) + ": " +
								 res.body());

	auto root = nlohmann::json::parse(res.body(), nullptr, false);
	if (root.is_discarded())
		throw std::runtime_error("DeepSeek Platform returned non-JSON (WAF block?): " + res.body().substr(0, 200));

	int code = root.value("code", -1);
	if (code != 0)
		throw std::runtime_error("DeepSeek Platform API code=" + std::to_string(code) +
								 " msg=" + root.value("msg", ""));

	auto const& data = root["data"];
	int biz_code = data.value("biz_code", -1);
	if (biz_code != 0)
		throw std::runtime_error("DeepSeek Platform API biz_code=" + std::to_string(biz_code) +
								 " biz_msg=" + data.value("biz_msg", ""));

	return data["biz_data"];
}

} // namespace

nlohmann::json query_usage_amount(std::string_view user_token, std::string_view waf_cookie, int64_t start_sec,
								  int64_t end_sec, bool verify_tls) {
	std::ostringstream path;
	path << "/api/v0/usage/by_api_key/amount"
		 << "?start=" << start_sec << "&end=" << end_sec << "&tz=0";

	return web_api_get("platform.deepseek.com", path.str(), user_token, waf_cookie, verify_tls);
}

nlohmann::json query_usage_cost(std::string_view user_token, std::string_view waf_cookie, int64_t start_sec,
								int64_t end_sec, bool verify_tls) {
	std::ostringstream path;
	path << "/api/v0/usage/by_api_key/cost"
		 << "?start=" << start_sec << "&end=" << end_sec << "&tz=0";

	return web_api_get("platform.deepseek.com", path.str(), user_token, waf_cookie, verify_tls);
}

} // namespace platform::deepseek
