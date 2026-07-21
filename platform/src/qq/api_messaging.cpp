/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "platform/qq/api.h"
#include "platform/log.h"

namespace platform::qq {

// ─── send_private_message ─────────────────────────────────────────────────

bool api::send_private_message(std::string_view openid, std::string_view content, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	auto body = nlohmann::json{{"content", std::string(content)}, {"msg_type", 0}};
	std::string path = "/v2/users/" + std::string(openid) + "/messages";

	boost::asio::co_spawn(s_.io_context(),
						  execute("POST", path, body.dump(),
								  {{"Authorization", "QQBot " + tok}, {"Content-Type", "application/json"}},
								  "[send private]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

// ─── send_group_message ───────────────────────────────────────────────────

bool api::send_group_message(std::string_view group, std::string_view content, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	auto body = nlohmann::json{{"content", std::string(content)}, {"msg_type", 0}};
	std::string path = "/v2/groups/" + std::string(group) + "/messages";

	boost::asio::co_spawn(s_.io_context(),
						  execute("POST", path, body.dump(),
								  {{"Authorization", "QQBot " + tok}, {"Content-Type", "application/json"}},
								  "[send group]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

// ─── send_channel_message ─────────────────────────────────────────────────

bool api::send_channel_message(std::string_view channel_id, std::string_view content, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	auto body = nlohmann::json{{"content", std::string(content)}, {"msg_type", 0}};
	std::string path = "/channels/" + std::string(channel_id) + "/messages";

	boost::asio::co_spawn(s_.io_context(),
						  execute("POST", path, body.dump(),
								  {{"Authorization", "QQBot " + tok}, {"Content-Type", "application/json"}},
								  "[send channel]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

// ─── send_dms_message ─────────────────────────────────────────────────────

bool api::send_dms_message(std::string_view guild_id, std::string_view content, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	auto body = nlohmann::json{{"content", std::string(content)}, {"msg_type", 0}};
	std::string path = "/dms/" + std::string(guild_id) + "/messages";

	boost::asio::co_spawn(s_.io_context(),
						  execute("POST", path, body.dump(),
								  {{"Authorization", "QQBot " + tok}, {"Content-Type", "application/json"}},
								  "[send dms]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

// ─── create_dm_session ────────────────────────────────────────────────────

bool api::create_dm_session(std::string_view recipient_id, std::string_view source_guild_id, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	auto body =
		nlohmann::json{{"recipient_id", std::string(recipient_id)}, {"source_guild_id", std::string(source_guild_id)}};

	boost::asio::co_spawn(s_.io_context(),
						  execute("POST", "/users/@me/dms", body.dump(),
								  {{"Authorization", "QQBot " + tok}, {"Content-Type", "application/json"}},
								  "[create dm]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

// ─── Markdown send helpers ────────────────────────────────────────────────

static nlohmann::json make_md_body(std::string_view md_content) {
	return nlohmann::json{{"msg_type", static_cast<int>(message_type::markdown)},
						  {"markdown", {{"content", std::string(md_content)}}}};
}

bool api::send_private_md(std::string_view openid, std::string_view md_content, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/v2/users/" + std::string(openid) + "/messages";
	boost::asio::co_spawn(s_.io_context(),
						  execute("POST", path, make_md_body(md_content).dump(),
								  {{"Authorization", "QQBot " + tok}, {"Content-Type", "application/json"}},
								  "[send private md]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

bool api::send_group_md(std::string_view group, std::string_view md_content, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/v2/groups/" + std::string(group) + "/messages";
	boost::asio::co_spawn(s_.io_context(),
						  execute("POST", path, make_md_body(md_content).dump(),
								  {{"Authorization", "QQBot " + tok}, {"Content-Type", "application/json"}},
								  "[send group md]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

bool api::send_channel_md(std::string_view channel_id, std::string_view md_content, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/channels/" + std::string(channel_id) + "/messages";
	boost::asio::co_spawn(s_.io_context(),
						  execute("POST", path, make_md_body(md_content).dump(),
								  {{"Authorization", "QQBot " + tok}, {"Content-Type", "application/json"}},
								  "[send channel md]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

bool api::send_dms_md(std::string_view guild_id, std::string_view md_content, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/dms/" + std::string(guild_id) + "/messages";
	boost::asio::co_spawn(s_.io_context(),
						  execute("POST", path, make_md_body(md_content).dump(),
								  {{"Authorization", "QQBot " + tok}, {"Content-Type", "application/json"}},
								  "[send dms md]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

} // namespace platform::qq
