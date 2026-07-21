/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "types.h"
#include "platform/ws/ws_client.h"

#include <string>
#include <map>
#include <thread>
#include <future>
#include <mutex>
#include <atomic>
#include <memory>
#include <chrono>
#include <queue>
#include <optional>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>

namespace platform::qq {

struct session {
	enum struct op_code : int {
		dispatch = 0,
		heartbeat = 1,
		identify = 2,
		resume = 6,
		reconnect = 7,
		invalid_session = 9,
		hello = 10,
		heartbeat_ack = 11
	};

	static constexpr int intent_guilds = 1 << 0;
	static constexpr int intent_guild_members = 1 << 1;
	static constexpr int intent_guild_messages = 1 << 9;
	static constexpr int intent_guild_reactions = 1 << 10;
	static constexpr int intent_direct_message = 1 << 12;
	static constexpr int intent_group_and_c2c = 1 << 25;
	static constexpr int intent_interaction = 1 << 26;
	static constexpr int intent_message_audit = 1 << 27;

	static constexpr int default_intents =
		intent_guilds | intent_guild_members | intent_direct_message | intent_group_and_c2c | intent_interaction;

	static constexpr int default_heartbeat_ms = 45000;
	static constexpr int login_retry_delay_s = 5;
	static constexpr int reconnect_delay_s = 3;

	session(std::string_view app_id, std::string_view app_secret, bool verify_tls = true);
	~session();

	void start();
	void stop();

	bool is_connected() const {
		return connected_.load();
	}
	bool is_running() const {
		return running_.load();
	}

	void on_message(raw_event_handler handler);
	void on_connect(raw_connect_handler handler);

	// ── thread-safe accessors ──
	std::string app_id() const {
		return app_id_;
	}
	std::string app_secret() const {
		return app_secret_;
	}
	std::string access_token() const;
	std::string session_id() const;
	std::string gateway_url() const;
	int last_sequence() const {
		return last_sequence_.load();
	}
	boost::asio::io_context& io_context() {
		return ioc_;
	}

	void set_access_token(std::string tok);
	void set_gateway_url(std::string url);
	void set_session_id(std::string id);
	void clear_session_id();

	// ── message handler (for dispatch, thread-safe) ──
	raw_event_handler get_message_handler() const {
		std::lock_guard<std::mutex> lk(state_mutex_);
		return raw_event_handler_;
	}

	// ── communicate handler (for transport) ──
	raw_connect_handler get_connect_handler() const {
		std::lock_guard<std::mutex> lk(state_mutex_);
		return raw_connect_handler_;
	}

	// ── internal (called by auth / transport / dispatch) ──
	boost::asio::awaitable<void> run();
	boost::asio::awaitable<bool> login_async();
	boost::asio::awaitable<bool> fetch_gateway_async();
	boost::asio::awaitable<void> run_websocket_async();
boost::asio::awaitable<void> token_refresh_timer_loop(int gen);
	boost::asio::awaitable<void> heartbeat_loop();
	boost::asio::awaitable<void> send_heartbeat_async();

	boost::asio::awaitable<std::pair<int, std::string>>
	http_request_async(std::string const& method, std::string const& host, std::string const& path, std::string body,
					   std::map<std::string, std::string> const& headers);

	void close_websocket();
	void reset_websocket();
	void send_heartbeat();

	// atomic state — safe to read without lock
	std::atomic<bool> running_{false};
	std::atomic<bool> connected_{false};
	std::atomic<bool> ws_connected_{false};
	std::atomic<int> last_sequence_{0};
	int heartbeat_interval_ms_ = default_heartbeat_ms;

	boost::asio::io_context ioc_;
	boost::asio::ssl::context ssl_ctx_{boost::asio::ssl::context::tlsv12_client};
	std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_guard_;
	::platform::ws::ws ws_client_{ioc_, ssl_ctx_};
	boost::asio::steady_timer heartbeat_timer_{ioc_};
	boost::asio::steady_timer token_refresh_timer_{ioc_};

	std::chrono::system_clock::time_point access_token_expires_at_;
	std::atomic<int> token_refresh_generation_{0};

	std::thread ioc_thread_;
	mutable std::mutex state_mutex_;

	private:
	// Immutable after construction
	std::string app_id_;
	std::string app_secret_;
	bool verify_tls_ = true;

	// Mutable — access via thread-safe getters/setters
	std::string access_token_;
	std::string gateway_url_;
	std::string last_session_id_;

	raw_event_handler raw_event_handler_;
	raw_connect_handler raw_connect_handler_;
};

} // namespace platform::qq
