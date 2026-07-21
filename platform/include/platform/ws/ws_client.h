/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include <string>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <optional>

namespace platform::ws {
struct ws {
	ws(boost::asio::io_context& ioc, boost::asio::ssl::context& ssl_ctx);
	~ws() = default;

	boost::asio::awaitable<void> connect_async(std::string const& host, std::string const& path, int port);
	boost::asio::awaitable<std::string> read_async();
	boost::asio::awaitable<void> write_async(std::string const& payload);

	void close();
	void reset();
	bool is_open() const;
	bool has_stream() const;

	private:
	boost::asio::io_context& ioc_;
	boost::asio::ssl::context& ssl_ctx_;
	boost::asio::ip::tcp::resolver resolver_;
	std::optional<boost::beast::websocket::stream<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>> ws_;
	boost::beast::flat_buffer read_buffer_;
};

} // namespace platform::ws
