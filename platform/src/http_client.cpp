/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "platform/http_client.h"
#include "platform/log.h"

namespace {
constexpr int default_port = 443;
constexpr int http_version = 11;
} // namespace

namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

namespace platform {
namespace detail {

// ─── Connection pool (keep-alive) ─────────────────────────────────────────

namespace {

struct pooled_stream {
	ssl::stream<tcp::socket> ssl_stream;
	std::chrono::steady_clock::time_point last_used;

	explicit pooled_stream(boost::asio::io_context& ioc, ssl::context& ctx) : ssl_stream(ioc, ctx) {
	}
};

std::mutex pool_mutex_;
std::unordered_map<std::string, std::shared_ptr<pooled_stream>> pool_;

std::shared_ptr<pooled_stream> pool_acquire(boost::asio::io_context& ioc, ssl::context& ctx, std::string const& host) {
	std::lock_guard<std::mutex> lk(pool_mutex_);
	auto it = pool_.find(host);
	if (it != pool_.end()) {
		auto s = std::move(it->second);
		pool_.erase(it);
		// Probe liveness with a non-blocking peek read on the raw TCP socket.
		// ONLY reuse when would_block (no data queued).  If peek returns data
		// the kernel buffer may hold a TLS close_notify alert — the SSL layer
		// would then fail the next read with "stream truncated".  Discard.
		boost::beast::error_code probe_ec;
		auto& sock = s->ssl_stream.next_layer();
		sock.non_blocking(true);
		char peeked{};
		try {
			sock.receive(boost::asio::buffer(&peeked, 1), boost::asio::socket_base::message_peek, probe_ec);
		} catch (...) {
			if (!probe_ec)
				probe_ec = boost::asio::error::fault;
		}
		sock.non_blocking(false);
		// Only would_block / try_again means clean socket (no data pending).
		if (probe_ec == boost::asio::error::would_block || probe_ec == boost::asio::error::try_again) {
			platform::log::debug("[http] pool reuse " + host);
			s->last_used = std::chrono::steady_clock::now();
			return s;
		}
		// Any other outcome (data peeked, eof, reset, ...) — discard connection.
		if (!probe_ec) {
			platform::log::debug("[http] pool evict " + host + " (data pending — possible TLS close_notify)");
		} else {
			platform::log::debug("[http] pool evict " + host + " (" + probe_ec.message() + ")");
		}
	}
	auto s = std::make_shared<pooled_stream>(ioc, ctx);
	s->last_used = std::chrono::steady_clock::now();
	return s;
}

void pool_release(std::string const& host, std::shared_ptr<pooled_stream> s) {
	if (!s) {
		return;
	}
	std::lock_guard<std::mutex> lk(pool_mutex_);
	// Limit pool size per host
	if (pool_.count(host) >= 3) {
		return;
	}
	platform::log::debug("[http] pool keep " + host);
	s->last_used = std::chrono::steady_clock::now();
	pool_[host] = std::move(s);
}

void pool_remove(std::string const& host) {
	std::lock_guard<std::mutex> lk(pool_mutex_);
	pool_.erase(host);
}

} // namespace

// ─── http_request_async ──────────────────────────────────────────────────

awaitable<std::pair<int, std::string>> http_request_async(std::string const& method, std::string const& host_param,
														  std::string const& path, std::string body,
														  std::map<std::string, std::string> const& headers,
														  bool verify_tls) {
	std::pair<int, std::string> result = {-1, ""};
	auto executor = co_await boost::asio::this_coro::executor;

	// Per-request ssl context (lightweight; pooled streams carry their own)
	ssl::context ssl_ctx{ssl::context::tlsv12_client};
	if (!verify_tls) {
		ssl_ctx.set_verify_mode(ssl::verify_none);
	}

	auto& ioc = static_cast<boost::asio::io_context&>(boost::asio::query(executor, boost::asio::execution::context));
	auto pool_stream = pool_acquire(ioc, ssl_ctx, host_param);
	bool reused = pool_stream->ssl_stream.next_layer().is_open();

	try {
		if (!reused) {
			tcp::resolver resolver{executor};

			std::string sni_host = host_param;
			if (!sni_host.empty() && sni_host.front() == '[' && sni_host.back() == ']') {
				sni_host = sni_host.substr(1, sni_host.size() - 2);
			}
			if (!SSL_set_tlsext_host_name(pool_stream->ssl_stream.native_handle(), sni_host.c_str())) {
				throw std::runtime_error("SSL_set_tlsext_host_name failed");
			}
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
			if (!SSL_set1_host(pool_stream->ssl_stream.native_handle(), sni_host.c_str())) {
				platform::log::error("[http_client] SSL_set1_host failed");
			}
#endif

			auto const results =
				co_await resolver.async_resolve(host_param, std::to_string(default_port), boost::asio::use_awaitable);
			co_await boost::asio::async_connect(pool_stream->ssl_stream.next_layer(), results,
												boost::asio::use_awaitable);
			co_await pool_stream->ssl_stream.async_handshake(ssl::stream_base::client, boost::asio::use_awaitable);
		}

		http::verb verb = [m = std::string(method)]() mutable {
			std::transform(m.begin(), m.end(), m.begin(), ::tolower);
			std::string method = std::move(m);
			if (method == "delete") {
				return http::verb::delete_;
			}
			auto ev = magic_enum::enum_cast<http::verb>(method);
			return ev.value_or(http::verb::get);
		}();

		if (verb == http::verb::get || body.empty()) {
			http::request<http::empty_body> req{verb, path, http_version};
			req.set(http::field::host, host_param);
			req.set(http::field::user_agent, "qqbot-cpp/1.0");
			req.set(http::field::connection, "keep-alive");
			for (auto const& [key, value] : headers) {
				req.set(key, value);
			}
			co_await http::async_write(pool_stream->ssl_stream, req, boost::asio::use_awaitable);
		} else {
			http::request<http::string_body> req{verb, path, http_version};
			req.set(http::field::host, host_param);
			req.set(http::field::user_agent, "qqbot-cpp/1.0");
			req.set(http::field::connection, "keep-alive");
			for (auto const& [key, value] : headers) {
				req.set(key, value);
			}
			req.body() = std::move(body);
			req.prepare_payload();
			co_await http::async_write(pool_stream->ssl_stream, req, boost::asio::use_awaitable);
		}

		beast::flat_buffer buffer;
		http::response<http::dynamic_body> res;
		co_await http::async_read(pool_stream->ssl_stream, buffer, res, boost::asio::use_awaitable);

		result.first = static_cast<int>(res.result_int());
		result.second = beast::buffers_to_string(res.body().data());

		// Check if server wants to close
		bool keep = true;
		if (auto conn = res.find(http::field::connection);
			conn != res.end() && beast::iequals(conn->value(), "close")) {
			keep = false;
		}
		if (result.first >= 400) {
			keep = false; // error: don't reuse
		}

		if (keep) {
			pool_release(host_param, std::move(pool_stream));
		} else {
			boost::beast::error_code ec;
			co_await pool_stream->ssl_stream.async_shutdown(boost::asio::redirect_error(boost::asio::use_awaitable, ec));
		}
	} catch (beast::system_error const& e) {
		platform::log::error(std::string("[http_client] beast::system_error: ") + e.what());
		boost::beast::error_code ec;
		pool_stream->ssl_stream.next_layer().close(ec);
	} catch (std::exception const& e) {
		platform::log::error(std::string("[http_client] std::exception: ") + e.what());
		boost::beast::error_code ec;
		pool_stream->ssl_stream.next_layer().close(ec);
	}

	co_return result;
}

} // namespace detail
} // namespace platform
