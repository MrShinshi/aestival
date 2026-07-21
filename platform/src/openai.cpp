/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "platform/openai.h"
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

namespace platform::openai {
namespace {
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

static std::pair<std::string, std::string> parse_url(std::string const& u) {
	std::string s = u;
	if (s.starts_with("https://"))
		s = s.substr(8);
	else if (s.starts_with("http://"))
		s = s.substr(7);
	auto p = s.find('/');
	if (p != std::string::npos)
		return {s.substr(0, p), s.substr(p)};
	return {s, ""};
}

static raw_chat_response send_chat_request(std::string_view api_key, std::string_view model,
										   std::string const& base_url, nlohmann::json const& messages,
										   nlohmann::json const* tools, bool verify_tls) {
	auto [host, base_path] = parse_url(base_url);
	if (host.empty())
		throw std::runtime_error("invalid base_url");

	boost::asio::io_context ioc;
	ssl::context ssl_ctx = platform::detail::make_ssl_ctx(verify_tls);
	tcp::resolver resolver(ioc);
	beast::ssl_stream<beast::tcp_stream> stream(ioc, ssl_ctx);

	if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
		throw std::runtime_error("failed to set TLS host name");

	auto const results = resolver.resolve(host, "443");
	beast::get_lowest_layer(stream).connect(results);
	stream.handshake(ssl::stream_base::client);

	nlohmann::json body = {{"model", std::string(model)}, {"messages", messages}, {"stream", false}};
	if (tools && tools->is_array() && !tools->empty())
		body["tools"] = *tools;

	std::string path = base_path + "/chat/completions";
	http::request<http::string_body> req{http::verb::post, path, 11};
	req.set(http::field::host, host);
	req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
	req.set(http::field::authorization, "Bearer " + std::string(api_key));
	req.set(http::field::content_type, "application/json");
	req.set(http::field::accept, "application/json");
	try {
		req.body() = body.dump();
	} catch (nlohmann::json::exception const&) {
		for (auto& m : body["messages"]) {
			auto& c = m["content"];
			if (c.is_string())
				c = platform::detail::sanitize_utf8(c.get<std::string>());
		}
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
		throw std::runtime_error("OpenAI HTTP " + std::to_string(static_cast<int>(res.result())) + ": " + res.body());

	auto root = nlohmann::json::parse(platform::detail::sanitize_utf8(res.body()), nullptr, false);
	auto const choices = root.find("choices");
	if (choices == root.end() || !choices->is_array() || choices->empty())
		throw std::runtime_error("OpenAI response missing choices");

	auto const& first = (*choices)[0];
	auto const msg = first.find("message");
	if (msg == first.end() || !msg->is_object())
		throw std::runtime_error("OpenAI response missing message");

	raw_chat_response result;
	result.content = msg->value("content", "");
	auto tcs = msg->find("tool_calls");
	if (tcs != msg->end() && tcs->is_array())
		result.tool_calls = *tcs;
	auto usage = root.find("usage");
	if (usage != root.end() && usage->is_object())
		result.usage = *usage;
	return result;
}
} // namespace

raw_chat_response chat(nlohmann::json const& messages, nlohmann::json const* tools, std::string_view api_key,
					   std::string_view model, std::string_view base_url, bool verify_tls) {
	return send_chat_request(api_key, model, std::string(base_url), messages, tools, verify_tls);
}

std::string make_time_string() {
	return platform::detail::make_time_string();
}

} // namespace platform::openai
