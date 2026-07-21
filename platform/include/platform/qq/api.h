/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once
#include "session.h"

#include <functional>
#include <string>
#include <string_view>
#include <map>

namespace platform::qq {

using api_callback = std::function<void(int http_status, std::string body)>;

struct api {
	explicit api(session& s);

	// Lifecycle pass-through
	void stop();

	// ── Messaging (plain text, msg_type=0) ──
	// All send_* return true if the request was queued. Pass a callback
	// to receive the HTTP response + parsed message_id / media_id etc.
	bool send_private_message(std::string_view openid, std::string_view content, api_callback cb = {});
	bool send_group_message(std::string_view group_id, std::string_view content, api_callback cb = {});
	bool send_channel_message(std::string_view channel_id, std::string_view content, api_callback cb = {});
	bool send_dms_message(std::string_view guild_id, std::string_view content, api_callback cb = {});
	bool create_dm_session(std::string_view recipient_id, std::string_view source_guild_id, api_callback cb = {});

	// ── Messaging (Markdown, msg_type=2) ──
	bool send_private_md(std::string_view openid, std::string_view md_content, api_callback cb = {});
	bool send_group_md(std::string_view group_id, std::string_view md_content, api_callback cb = {});
	bool send_channel_md(std::string_view channel_id, std::string_view md_content, api_callback cb = {});
	bool send_dms_md(std::string_view guild_id, std::string_view md_content, api_callback cb = {});

	// ── Media ──
	bool upload_user_file(std::string_view user_openid, int file_type, std::string const& url_or_base64,
						  api_callback cb = {});
	bool upload_group_file(std::string_view group_openid, int file_type, std::string const& url_or_base64,
						   api_callback cb = {});
	bool recall_channel_message(std::string_view channel_id, std::string_view message_id, api_callback cb = {});
	bool recall_private_message(std::string_view user_openid, std::string_view message_id, api_callback cb = {});
	bool recall_group_message(std::string_view group_openid, std::string_view message_id, api_callback cb = {});

	// ── Guild / Channel ──
	bool get_user_guilds(api_callback cb = {});
	bool get_guild(std::string_view guild_id, api_callback cb = {});
	bool get_guild_channels(std::string_view guild_id, api_callback cb = {});
	bool get_channel(std::string_view channel_id, api_callback cb = {});
	bool create_channel(std::string_view guild_id, std::string const& payload, api_callback cb = {});
	bool modify_channel(std::string_view channel_id, std::string const& payload, api_callback cb = {});
	bool delete_channel(std::string_view channel_id, api_callback cb = {});

	// ── Members ──
	bool get_guild_members(std::string_view guild_id, int limit = 100, std::string_view after = "0",
						   api_callback cb = {});
	bool get_guild_member(std::string_view guild_id, std::string_view user_id, api_callback cb = {});
	bool delete_guild_member(std::string_view guild_id, std::string_view user_id, api_callback cb = {});

	// ── Roles ──
	bool get_roles(std::string_view guild_id, api_callback cb = {});
	bool create_role(std::string_view guild_id, std::string const& payload, api_callback cb = {});
	bool modify_role(std::string_view guild_id, std::string_view role_id, std::string const& payload,
					 api_callback cb = {});
	bool delete_role(std::string_view guild_id, std::string_view role_id, api_callback cb = {});
	bool add_member_role(std::string_view guild_id, std::string_view user_id, std::string_view role_id,
						 api_callback cb = {});
	bool remove_member_role(std::string_view guild_id, std::string_view user_id, std::string_view role_id,
							api_callback cb = {});

	private:
	session& s_;

	std::string get_token() const;
	boost::asio::awaitable<void> execute(std::string method, std::string path, std::string body,
										 std::map<std::string, std::string> headers, char const* log_prefix,
										 api_callback cb);
};

} // namespace platform::qq
