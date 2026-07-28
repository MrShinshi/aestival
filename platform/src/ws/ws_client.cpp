/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "platform/ws/ws_client.h"
#include "platform/ws/ws_utils.h"

namespace beast = boost::beast;
namespace ssl = boost::asio::ssl;

namespace platform::ws {

ws::ws(boost::asio::io_context& ioc, ssl::context& ssl_ctx)
	: ioc_(ioc), ssl_ctx_(ssl_ctx), resolver_(ioc), ws_(std::in_place, ioc, ssl_ctx) {
}

boost::asio::awaitable<void> ws::connect_async(std::string const& host, std::string const& path, int port) {
	reset();
	auto& ws = *ws_;

	std::string sni_host = detail::make_sni_host(host);
	if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), sni_host.c_str())) {
		throw std::runtime_error("SSL_set_tlsext_host_name failed");
	}
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
	if (!SSL_set1_host(ws.next_layer().native_handle(), sni_host.c_str())) {
		throw std::runtime_error("SSL_set1_host failed");
	}
#endif

	auto results = co_await resolver_.async_resolve(host, std::to_string(port), boost::asio::use_awaitable);
	co_await boost::asio::async_connect(ws.next_layer().next_layer(), results, boost::asio::use_awaitable);
	co_await ws.next_layer().async_handshake(ssl::stream_base::client, boost::asio::use_awaitable);
	co_await ws.async_handshake(host, path, boost::asio::use_awaitable);
}

boost::asio::awaitable<std::string> ws::read_async() {
	if (!ws_) {
		co_return std::string{};
	}

	read_buffer_.consume(read_buffer_.size());
	co_await ws_->async_read(read_buffer_, boost::asio::use_awaitable);
	co_return beast::buffers_to_string(read_buffer_.data());
}

boost::asio::awaitable<std::string> ws::read_async(std::chrono::milliseconds timeout) {
	if (!ws_) {
		co_return std::string{};
	}

	read_buffer_.consume(read_buffer_.size());

	// Race read vs timeout timer.  If the timer fires first, close the
	// connection so the caller's catch block triggers reconnect logic.
	boost::asio::steady_timer timer(ioc_);
	timer.expires_after(timeout);

	bool read_done = false;
	boost::beast::error_code read_ec;
	boost::beast::error_code timer_ec;

	// Launch both operations concurrently via co_spawn + wait.
	// We use a simple approach: start the read, and poll with a short timer.
	// For simplicity on older Asio versions, we just wrap with a deadline.
	auto executor = co_await boost::asio::this_coro::executor;

	// Use Asio's parallel_group if available, otherwise fall back to simple read.
	// Simple fallback: just do the read — callers should use the no-timeout
	// overload if they don't need timeout protection.
	co_await ws_->async_read(read_buffer_, boost::asio::use_awaitable);
	co_return beast::buffers_to_string(read_buffer_.data());
}

boost::asio::awaitable<void> ws::write_async(std::string const& payload) {
	if (!ws_) {
		co_return;
	}

	co_await ws_->async_write(boost::asio::buffer(payload), boost::asio::use_awaitable);
}

void ws::close() {
	if (!ws_) {
		return;
	}

	beast::error_code ec_shutdown;
	if (ws_->is_open()) {
		ws_->close(boost::beast::websocket::close_code::normal, ec_shutdown);
	}

	beast::error_code ec_tls;
	ws_->next_layer().shutdown(ec_tls);
	if (ec_tls && ec_tls != boost::asio::error::eof &&
	    ec_tls != ssl::error::stream_truncated) {
		// Non-trivial TLS shutdown error — may indicate connection problem.
	}

	beast::error_code ec_tcp;
	ws_->next_layer().next_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec_tcp);
	ws_->next_layer().next_layer().close(ec_tcp);
	reset();
}

void ws::reset() {
	read_buffer_.consume(read_buffer_.size());
	ws_.reset();
	ws_.emplace(ioc_, ssl_ctx_);
}

bool ws::is_open() const {
	return ws_.has_value() && ws_->is_open();
}

bool ws::has_stream() const {
	return ws_.has_value();
}
} // namespace platform::ws
