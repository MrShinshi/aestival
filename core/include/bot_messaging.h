/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <string>
#include <string_view>

namespace client {

// Minimal interface covering all the `api` methods that plugin_context
// and agent_controller actually use.  Both `api` (QQ backend) and
// `console_api` (CLI test backend) satisfy this.
struct bot_messaging {
	virtual ~bot_messaging() = default;

	virtual void stop() = 0;

	// plain-text C2C (used by agent_controller::notify_startup)
	virtual bool send_private_message(std::string_view openid, std::string_view content) = 0;

	// Markdown variants — used by plugin_context::reply()
	virtual bool send_private_md(std::string_view openid, std::string_view content) = 0;
	virtual bool send_group_md(std::string_view group_id, std::string_view content) = 0;
	virtual bool send_channel_md(std::string_view channel_id, std::string_view content) = 0;
	virtual bool send_dms_md(std::string_view guild_id, std::string_view content) = 0;
};

} // namespace client
