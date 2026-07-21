/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "platform/qq/session.h"
#include "platform/qq/dispatch.h"
#include "platform/log.h"
#include "platform/ws/ws_utils.h"

namespace platform::qq {

// ─── helpers ──────────────────────────────────────────────────────────────

static void fire_connected(session& s, char const* reason) {
	auto h = s.get_connect_handler();
	if (h) {
		h(true, reason);
	}
	s.connected_ = true;
}

static void fire_disconnected(session& s, char const* reason) {
	auto h = s.get_connect_handler();
	if (h) {
		h(false, reason);
	}
}

// ─── run_websocket_async ──────────────────────────────────────────────────

boost::asio::awaitable<void> session::run_websocket_async() {
	std::string gw = gateway_url();
	std::string last_sid = session_id();

	std::string host, path;
	int port = default_port;
	if (!platform::ws::detail::parse_url(gw, host, path, port, default_port)) {
		log::error("[asio] url parse failed");
		co_return;
	}

	try {
		co_await ws_client_.connect_async(host, path, port);
		ws_connected_ = true;
		log::info("[asio] ws handshake successful!");

		bool identified = false, heartbeat_started = false;

		while (running_ && ws_connected_) {
			std::string data = co_await ws_client_.read_async();
			auto pos = data.find('{');
			if (pos == std::string::npos) {
				continue;
			}

			auto root = nlohmann::json::parse(data.substr(pos), nullptr, false);
			if (root.is_discarded()) {
				continue;
			}

			int op = root.value("op", 0);
			std::string event = root.value("t", "");
			if (!root["s"].is_null()) {
				last_sequence_ = root.value("s", 0);
			}

			// ── OpCode 10: Hello ──────────────────────────────────────
			if (op == static_cast<int>(op_code::hello)) {
				if (root.contains("d")) {
					heartbeat_interval_ms_ = root["d"].value("heartbeat_interval", default_heartbeat_ms);
				}

				if (!identified) {
					if (!last_sid.empty()) {
						auto resume_payload = nlohmann::json{{"op", static_cast<int>(op_code::resume)},
															 {"d",
															  {{"token", "QQBot " + access_token()},
															   {"session_id", last_sid},
															   {"seq", last_sequence_.load()}}}};
						co_await ws_client_.write_async(resume_payload.dump());
						log::info("[asio] resume sent");
					} else {
						auto tok = access_token();
						auto identify = nlohmann::json{
							{"op", static_cast<int>(op_code::identify)},
							{"d", {{"token", "QQBot " + tok}, {"intents", default_intents}, {"shard", {0, 1}}}}};
						co_await ws_client_.write_async(identify.dump());
					}
					identified = true;
				}

				if (!heartbeat_started) {
					heartbeat_started = true;
						boost::asio::co_spawn(ioc_, heartbeat_loop(), boost::asio::detached);
				}

				// ── OpCode 9: Invalid Session ────────────────────────────
				// Resume rejected → clear session_id, reconnect with fresh Identify
			} else if (op == static_cast<int>(op_code::invalid_session)) {
				log::warn("[asio] invalid session — will re-identify");
				clear_session_id();
				break;

				// ── OpCode 0: Dispatch ───────────────────────────────────
			} else if (op == static_cast<int>(op_code::dispatch)) {
				std::string d = root.contains("d") ? root["d"].dump() : "{}";

				if (event == "READY") {
					try {
						auto rd = nlohmann::json::parse(d);
						auto sid = rd.value("session_id", "");
						if (!sid.empty()) {
							set_session_id(std::move(sid));
						}
					} catch (...) {
					}
					fire_connected(*this, "OK");
					dispatch_event(*this, event, d);
				} else if (event == "RESUMED") {
					log::info("[asio] RESUMED — events replayed");
					fire_connected(*this, "OK (resumed)");
				} else {
					dispatch_event(*this, event, d);
				}

				// ── OpCode 11: Heartbeat ACK ─────────────────────────────
			} else if (op == static_cast<int>(op_code::heartbeat_ack)) {
				// silent

				// ── OpCode 7: Reconnect ──────────────────────────────────
			} else if (op == static_cast<int>(op_code::reconnect)) {
				log::warn("[asio] server requested reconnect");
				break;
			}
		}
	} catch (boost::beast::system_error const& e) {
		if (!platform::ws::detail::is_expected_shutdown_error(e.code())) {
			std::ostringstream s;
			s << "[asio] ws error: " << e.what();
			log::error(s.str());
		} else {
			log::info("[asio] ws closed");
		}
	} catch (std::exception const& e) {
		std::ostringstream s;
		s << "[asio] ws exception: " << e.what();
		log::error(s.str());
	}

	// basic_waitable_timer::cancel() no longer accepts an error_code parameter in recent Boost versions
	heartbeat_timer_.cancel();
	ws_connected_ = false;
	connected_ = false;
	fire_disconnected(*this, "disconnected");
	close_websocket();
}

// ─── heartbeat_loop ──────────────────────────────────────────────────────

boost::asio::awaitable<void> session::heartbeat_loop() {
	bool first = true;
	try {
		while (running_ && ws_connected_) {
			heartbeat_timer_.expires_after(std::chrono::milliseconds(heartbeat_interval_ms_));
			boost::beast::error_code ec;
			co_await heartbeat_timer_.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));
			if (ec || !running_ || !ws_connected_ || !ws_client_.is_open()) {
				break;
			}

			if (first) {
				auto msg = nlohmann::json{{"op", static_cast<int>(op_code::heartbeat)}, {"d", nullptr}}.dump();
				co_await ws_client_.write_async(std::move(msg));
				first = false;
			} else {
				auto msg = nlohmann::json{{"op", static_cast<int>(op_code::heartbeat)}, {"d", last_sequence_.load()}}.dump();
				co_await ws_client_.write_async(std::move(msg));
			}
		}
	} catch (std::exception const& e) {
		if (running_) {
			std::ostringstream s;
			s << "[asio] heartbeat failed: " << e.what();
			log::error(s.str());
		}
	}
}

// ─── close / reset / send_heartbeat ──────────────────────────────────────

void session::close_websocket() {
	// Cancel heartbeat timer (cancel() no longer takes an error_code)
	heartbeat_timer_.cancel();
	ws_client_.close();
}

void session::reset_websocket() {
	ws_client_.reset();
}

void session::send_heartbeat() {
	if (!running_ || !ws_connected_) {
		return;
	}
	boost::asio::co_spawn(ioc_, send_heartbeat_async(), boost::asio::detached);
}

boost::asio::awaitable<void> session::send_heartbeat_async() {
	if (!ws_connected_ || !ws_client_.is_open()) {
		co_return;
	}
	try {
		auto msg = nlohmann::json{{"op", static_cast<int>(op_code::heartbeat)}, {"d", last_sequence_.load()}}.dump();
		co_await ws_client_.write_async(std::move(msg));
	} catch (std::exception const& e) {
		std::ostringstream s;
		s << "[asio] heartbeat failed: " << e.what();
		log::error(s.str());
	}
}
} // namespace platform::qq
