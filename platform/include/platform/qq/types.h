/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <cstdint>
#include <string>
#include <functional>

#include <magic_enum/magic_enum.hpp>
#include <nlohmann/json.hpp>

// ─── nlohmann enum adapter ────────────────────────────────────────────────
namespace nlohmann {

template <typename E, typename = std::enable_if_t<std::is_enum_v<E>>> void to_json(json& j, E e) {
	j = static_cast<std::underlying_type_t<E>>(e);
}

template <typename E, typename = std::enable_if_t<std::is_enum_v<E>>> void from_json(json const& j, E& e) {
	e = static_cast<E>(j.get<std::underlying_type_t<E>>());
}

} // namespace nlohmann

// ─── QQ API domain enums ──────────────────────────────────────────────────
namespace platform::qq::qq_api {

enum struct http_status : std::int32_t {
	unknown = -1,
	ok = 200,
	created = 201,
	no_content = 204,
	unauthorized = 401,
	not_found = 404,
	method_not_allowed = 405,
	too_many_requests = 429,
	server_error = 500
};

enum struct error_code : std::int32_t {
	none = 0,
	msg_limit_exceed = 22009,
	upload_media_info_fail = 304082,
	convert_media_info_fail = 304083,
	url_not_registered = 304003,
	gateway_not_connected = 304018,
	message_need_audit = 304023,
	message_need_audit2 = 304024,
	message_blocked = 304025,
	reply_expired = 304027,
	not_at_bot = 304028,
	dm_closed = 304031,
	channel_active_limit_exceeded = 304045,
	not_allowed_in_channel = 304046,
	not_allowed_in_channel2 = 304048,
	get_message_failed = 306003,
	no_recall_permission = 306004,
	recall_time_exceeded = 306011,
	not_same_guild = 50038,
	cannot_reply_to_self = 50045,
	not_at_bot2 = 50046,
	edit_message_error = 50053,
	invalid_markdown = 50055,
	bot_not_in_guild = 610007,
	api_already_authorized = 610010,
	content_sensitive = 1100101,
	internal_error = 1100300
};

inline http_status to_http_status(int code) {
	auto status = magic_enum::enum_cast<http_status>(code);
	return status.has_value() ? status.value() : http_status::unknown;
}

inline error_code to_error_code(int code) {
	auto ec = magic_enum::enum_cast<error_code>(code);
	return ec.has_value() ? ec.value() : error_code::none;
}

} // namespace platform::qq::qq_api

// ─── QQ session message types ─────────────────────────────────────────────
namespace platform::qq {

// Parsed message — platform fills this, adapter copies to core::message_event
struct parsed_message {
	std::string message_id, content, sender_id, sender_nick;
	std::string user_openid, group_id, channel_id, guild_id;
	bool is_private = false, is_group = false, is_guild = false;
	bool was_at_mentioned = false;
};

using raw_event_handler = std::function<void(std::string_view event_type, parsed_message&& msg)>;
using raw_connect_handler = std::function<void(bool connected, std::string_view reason)>;

enum struct message_type : int { text = 0, markdown = 2, ark = 3, embed = 4, media = 7 };

// ─── HTTP constants ───────────────────────────────────────────────────────
inline constexpr int default_port = 443;
inline constexpr int http_version = 11; // HTTP/1.1

// ─── dispatch payloads ────────────────────────────────────────────────────

struct c2c_message_payload {
	std::string id, content, user_openid;
};

struct group_message_payload {
	std::string id, group_openid, content, member_openid, sender_nick;
};

struct guild_message_payload {
	std::string id, content, channel_id, guild_id, user_id, username;
};

struct dm_message_payload {
	std::string id, content, guild_id, user_id;
};

struct guild_member_payload {
	std::string guild_id, user_id, username, nick;
};

inline void from_json(nlohmann::json const& j, c2c_message_payload& v) {
	v.id = j.value("id", "");
	v.content = j.value("content", "");
	auto a = j.find("author");
	if (a != j.end() && a->is_object()) {
		v.user_openid = a->value("user_openid", "");
	}
}

inline void from_json(nlohmann::json const& j, group_message_payload& v) {
	v.id = j.value("id", "");
	v.group_openid = j.value("group_openid", "");
	v.content = j.value("content", "");
	auto a = j.find("author");
	if (a != j.end() && a->is_object()) {
		v.member_openid = a->value("member_openid", "");
		v.sender_nick = a->value("username", "");
		if (v.sender_nick.empty()) {
			v.sender_nick = "用户" + v.member_openid.substr(0, 8);
		}
	}
}

inline void from_json(nlohmann::json const& j, guild_message_payload& v) {
	v.id = j.value("id", "");
	v.content = j.value("content", "");
	v.channel_id = j.value("channel_id", "");
	v.guild_id = j.value("guild_id", "");
	auto a = j.find("author");
	if (a != j.end() && a->is_object()) {
		v.user_id = a->value("id", "");
		v.username = a->value("username", "");
	}
}

inline void from_json(nlohmann::json const& j, dm_message_payload& v) {
	v.id = j.value("id", "");
	v.content = j.value("content", "");
	v.guild_id = j.value("guild_id", "");
	auto a = j.find("author");
	if (a != j.end() && a->is_object()) {
		v.user_id = a->value("id", "");
	}
}

inline void from_json(nlohmann::json const& j, guild_member_payload& v) {
	v.guild_id = j.value("guild_id", "");
	auto u = j.find("user");
	if (u != j.end() && u->is_object()) {
		v.user_id = u->value("id", "");
		v.username = u->value("username", "");
	}
	v.nick = j.value("nick", "");
}

} // namespace platform::qq
