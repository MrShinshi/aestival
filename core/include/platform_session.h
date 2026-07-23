/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "message_types.h"

#include <boost/asio.hpp>

#include <functional>
#include <string>

namespace client {

// ─── platform_session ──────────────────────────────────────────────────────
// Abstract interface for platform-specific sessions (QQ, WeChat, etc.).
//
// Each agent instance owns one platform_session.  The session manages its
// own io_context + thread — the agent_controller never touches threading
// details; it only calls start()/stop() and registers callbacks.
//
// Implementations:
//   platform::qq::session  (existing — will implement this interface)
//   future: wechat, discord, telegram, etc.

struct platform_session {
	virtual ~platform_session() = default;

	// Lifecycle.
	virtual void start() = 0;
	virtual void stop() = 0;
	virtual bool is_running() const = 0;

	// Callbacks — set BEFORE calling start().
	virtual void on_message(message_handler handler) = 0;
	virtual void on_connect(connect_handler handler) = 0;

	// The io_context that drives this session.  Implementation guarantees
	// it outlives the session object.
	virtual boost::asio::io_context& io_context() = 0;

	// Human-readable platform name for logs / metrics.
	virtual std::string platform_name() const = 0;
};

} // namespace client
