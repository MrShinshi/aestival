/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <openssl/ssl.h>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <chrono>
#include <ctime>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <sys/time.h>
#endif

namespace platform::detail {

// Shared SSL context factory — used by deepseek.cpp and openai.cpp.
inline boost::asio::ssl::context make_ssl_ctx(bool verify_tls) {
	boost::asio::ssl::context ctx{boost::asio::ssl::context::tls_client};
	if (verify_tls)
		ctx.set_default_verify_paths();
	else
		ctx.set_verify_mode(boost::asio::ssl::verify_none);
	SSL_CTX_set_options(ctx.native_handle(), SSL_OP_NO_TICKET);
	return ctx;
}

// Set send/receive timeouts on a Beast stream's underlying TCP socket.
// Prevents sync HTTP operations from blocking a worker thread indefinitely
// when the remote server accepts the connection but never responds.
template <typename Stream>
inline void set_socket_timeout(Stream& stream, int seconds) {
	auto& sock = boost::beast::get_lowest_layer(stream).socket();
#ifdef _WIN32
	DWORD timeout_ms = static_cast<DWORD>(seconds) * 1000;
	setsockopt(sock.native_handle(), SOL_SOCKET, SO_RCVTIMEO,
			   reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
	setsockopt(sock.native_handle(), SOL_SOCKET, SO_SNDTIMEO,
			   reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
	struct timeval tv;
	tv.tv_sec = seconds;
	tv.tv_usec = 0;
	setsockopt(sock.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(sock.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

// Shared timestamp helper — used by deepseek.cpp and openai.cpp.
inline std::string make_time_string() {
	auto now = std::chrono::system_clock::now();
	auto now_t = std::chrono::system_clock::to_time_t(now);
	char timebuf[64] = {0};
	std::tm tmbuf{};
#if defined(_WIN32)
	bool ok = (localtime_s(&tmbuf, &now_t) == 0);
#else
	bool ok = (localtime_r(&now_t, &tmbuf) != nullptr);
#endif
	if (ok)
		std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tmbuf);
#if defined(_WIN32)
	else if (gmtime_s(&tmbuf, &now_t) == 0)
		std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S UTC", &tmbuf);
#endif
	return std::string(timebuf);
}

} // namespace platform::detail
