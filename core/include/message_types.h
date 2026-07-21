/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <nlohmann/json_fwd.hpp>
#include <functional>
#include <string>

namespace client {

struct message_event {
	std::string message_id;
	std::string content;
	std::string sender_id;
	std::string sender_nick;
	std::string user_openid;
	std::string group_id;
	std::string channel_id;
	std::string guild_id;
	std::string protocol = "qq";
	nlohmann::json metadata;
	bool is_private = false;
	bool is_group = false;
	bool is_guild = false;
	bool was_at_mentioned = false;
};

enum struct message_type : int { text = 0, markdown = 2, ark = 3, embed = 4, media = 7 };

using message_handler = std::function<void(message_event const&)>;
using connect_handler = std::function<void(bool connected, char const* reason)>;

} // namespace client
