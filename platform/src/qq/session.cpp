/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "platform/qq/session.h"
#include "platform/log.h"

namespace platform::qq {

// ─── ctor / dtor ──────────────────────────────────────────────────────────

session::session(std::string_view app_id, std::string_view app_secret, bool verify_tls)
	: app_id_(app_id), app_secret_(app_secret), verify_tls_(verify_tls) {
	if (!verify_tls_) {
		ssl_ctx_.set_verify_mode(boost::asio::ssl::verify_none);
	}
}

session::~session() {
	work_guard_.reset();
	if (ioc_thread_.joinable()) {
		ioc_thread_.join();
	}
}

// ─── thread-safe accessors ────────────────────────────────────────────────

std::string session::access_token() const {
	std::lock_guard<std::mutex> lk(state_mutex_);
	return access_token_;
}

std::string session::session_id() const {
	std::lock_guard<std::mutex> lk(state_mutex_);
	return last_session_id_;
}

std::string session::gateway_url() const {
	std::lock_guard<std::mutex> lk(state_mutex_);
	return gateway_url_;
}

void session::set_access_token(std::string tok) {
	std::lock_guard<std::mutex> lk(state_mutex_);
	access_token_ = std::move(tok);
}

void session::set_gateway_url(std::string url) {
	std::lock_guard<std::mutex> lk(state_mutex_);
	gateway_url_ = std::move(url);
}

void session::set_session_id(std::string id) {
	std::lock_guard<std::mutex> lk(state_mutex_);
	last_session_id_ = std::move(id);
}

void session::clear_session_id() {
	std::lock_guard<std::mutex> lk(state_mutex_);
	last_session_id_.clear();
}

// ─── start ────────────────────────────────────────────────────────────────

void session::start() {
	if (running_.exchange(true)) {
		return;
	}
	if (ioc_thread_.joinable()) {
		ioc_thread_.join();
	}

	connected_ = false;
	ws_connected_ = false;
	last_sequence_ = 0;
	heartbeat_interval_ms_ = default_heartbeat_ms;
	reset_websocket();

	// basic_waitable_timer::cancel() no longer accepts an error_code parameter in newer Boost.
	heartbeat_timer_.cancel();

	ioc_.restart();
	work_guard_.emplace(boost::asio::make_work_guard(ioc_));
	boost::asio::co_spawn(ioc_, run(), boost::asio::detached);

	ioc_thread_ = std::thread([this] { ioc_.run(); });
}

// ─── stop ─────────────────────────────────────────────────────────────────

void session::stop() {
	running_ = false;
	connected_ = false;
	ws_connected_ = false;

	// Post all timer/ws teardown to io_context to avoid races
	if (!ioc_.stopped()) {
		boost::asio::post(ioc_, [this] {
			// Cancel timers (cancel() no longer takes an error_code arg)
			heartbeat_timer_.cancel();
			token_refresh_timer_.cancel();
			close_websocket();
		});
		work_guard_.reset();
		boost::asio::post(ioc_, [this] { ioc_.stop(); });
	}

	if (ioc_thread_.joinable() && std::this_thread::get_id() != ioc_thread_.get_id()) {
		ioc_thread_.join();
		reset_websocket();
	}
}

// ─── handler registration ─────────────────────────────────────────────────

void session::on_message(raw_event_handler handler) {
	std::lock_guard<std::mutex> lk(state_mutex_);
	raw_event_handler_ = std::move(handler);
}

void session::on_connect(raw_connect_handler handler) {
	std::lock_guard<std::mutex> lk(state_mutex_);
	raw_connect_handler_ = std::move(handler);
}

} // namespace platform::qq
