/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "platform/qq/api.h"
#include "platform/log.h"

namespace platform::qq {

// ─── upload_user_file ─────────────────────────────────────────────────────

bool api::upload_user_file(std::string_view user_openid, int file_type, std::string const& url_or_base64,
						   api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	auto body = nlohmann::json{{"file_type", file_type}, {"url", url_or_base64}};
	std::string path = "/v2/users/" + std::string(user_openid) + "/files";

	boost::asio::co_spawn(s_.io_context(),
						  execute("POST", path, body.dump(),
								  {{"Authorization", "QQBot " + tok}, {"Content-Type", "application/json"}},
								  "[upload user file]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

// ─── upload_group_file ────────────────────────────────────────────────────

bool api::upload_group_file(std::string_view group_openid, int file_type, std::string const& url_or_base64,
							api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	auto body = nlohmann::json{{"file_type", file_type}, {"url", url_or_base64}};
	std::string path = "/v2/groups/" + std::string(group_openid) + "/files";

	boost::asio::co_spawn(s_.io_context(),
						  execute("POST", path, body.dump(),
								  {{"Authorization", "QQBot " + tok}, {"Content-Type", "application/json"}},
								  "[upload group file]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

// ─── recall_channel_message ───────────────────────────────────────────────

bool api::recall_channel_message(std::string_view channel_id, std::string_view message_id, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/channels/" + std::string(channel_id) + "/messages/" + std::string(message_id);

	boost::asio::co_spawn(
		s_.io_context(),
		execute("DELETE", path, "", {{"Authorization", "QQBot " + tok}}, "[recall channel]", std::move(cb)),
		boost::asio::detached);
	return true;
}

// ─── recall_private_message ───────────────────────────────────────────────

bool api::recall_private_message(std::string_view user_openid, std::string_view message_id, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/v2/users/" + std::string(user_openid) + "/messages/" + std::string(message_id);

	boost::asio::co_spawn(
		s_.io_context(),
		execute("DELETE", path, "", {{"Authorization", "QQBot " + tok}}, "[recall private]", std::move(cb)),
		boost::asio::detached);
	return true;
}

// ─── recall_group_message ─────────────────────────────────────────────────

bool api::recall_group_message(std::string_view group_openid, std::string_view message_id, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/v2/groups/" + std::string(group_openid) + "/messages/" + std::string(message_id);

	boost::asio::co_spawn(
		s_.io_context(),
		execute("DELETE", path, "", {{"Authorization", "QQBot " + tok}}, "[recall group]", std::move(cb)),
		boost::asio::detached);
	return true;
}

// ─── get_user_guilds ──────────────────────────────────────────────────────

bool api::get_user_guilds(api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	boost::asio::co_spawn(
		s_.io_context(),
		execute("GET", "/users/@me/guilds", "", {{"Authorization", "QQBot " + tok}}, "[guild list]", std::move(cb)),
		boost::asio::detached);
	return true;
}

// ─── get_guild ────────────────────────────────────────────────────────────

bool api::get_guild(std::string_view guild_id, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/guilds/" + std::string(guild_id);
	boost::asio::co_spawn(s_.io_context(),
						  execute("GET", path, "", {{"Authorization", "QQBot " + tok}}, "[guild]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

// ─── get_guild_channels ───────────────────────────────────────────────────

bool api::get_guild_channels(std::string_view guild_id, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/guilds/" + std::string(guild_id) + "/channels";
	boost::asio::co_spawn(s_.io_context(),
						  execute("GET", path, "", {{"Authorization", "QQBot " + tok}}, "[channels]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

// ─── get_channel ──────────────────────────────────────────────────────────

bool api::get_channel(std::string_view channel_id, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/channels/" + std::string(channel_id);
	boost::asio::co_spawn(s_.io_context(),
						  execute("GET", path, "", {{"Authorization", "QQBot " + tok}}, "[channel]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

// ─── create_channel ───────────────────────────────────────────────────────

bool api::create_channel(std::string_view guild_id, std::string const& payload, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/guilds/" + std::string(guild_id) + "/channels";
	boost::asio::co_spawn(s_.io_context(),
						  execute("POST", path, payload,
								  {{"Authorization", "QQBot " + tok}, {"Content-Type", "application/json"}},
								  "[create channel]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

// ─── modify_channel ───────────────────────────────────────────────────────

bool api::modify_channel(std::string_view channel_id, std::string const& payload, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/channels/" + std::string(channel_id);
	boost::asio::co_spawn(s_.io_context(),
						  execute("POST", path, payload,
								  {{"Authorization", "QQBot " + tok}, {"Content-Type", "application/json"}},
								  "[modify channel]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

// ─── delete_channel ───────────────────────────────────────────────────────

bool api::delete_channel(std::string_view channel_id, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/channels/" + std::string(channel_id);
	boost::asio::co_spawn(
		s_.io_context(),
		execute("DELETE", path, "", {{"Authorization", "QQBot " + tok}}, "[del channel]", std::move(cb)),
		boost::asio::detached);
	return true;
}

// ─── get_guild_members ────────────────────────────────────────────────────

bool api::get_guild_members(std::string_view guild_id, int limit, std::string_view after, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::ostringstream path;
	path << "/guilds/" << guild_id << "/members?limit=" << limit << "&after=" << after;

	boost::asio::co_spawn(
		s_.io_context(),
		execute("GET", path.str(), "", {{"Authorization", "QQBot " + tok}}, "[members]", std::move(cb)),
		boost::asio::detached);
	return true;
}

// ─── get_guild_member ─────────────────────────────────────────────────────

bool api::get_guild_member(std::string_view guild_id, std::string_view user_id, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/guilds/" + std::string(guild_id) + "/members/" + std::string(user_id);

	boost::asio::co_spawn(s_.io_context(),
						  execute("GET", path, "", {{"Authorization", "QQBot " + tok}}, "[member]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

// ─── delete_guild_member ──────────────────────────────────────────────────

bool api::delete_guild_member(std::string_view guild_id, std::string_view user_id, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/guilds/" + std::string(guild_id) + "/members/" + std::string(user_id);

	boost::asio::co_spawn(
		s_.io_context(),
		execute("DELETE", path, "", {{"Authorization", "QQBot " + tok}}, "[kick member]", std::move(cb)),
		boost::asio::detached);
	return true;
}

// ─── get_roles ────────────────────────────────────────────────────────────

bool api::get_roles(std::string_view guild_id, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/guilds/" + std::string(guild_id) + "/roles";
	boost::asio::co_spawn(s_.io_context(),
						  execute("GET", path, "", {{"Authorization", "QQBot " + tok}}, "[roles]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

// ─── create_role ──────────────────────────────────────────────────────────

bool api::create_role(std::string_view guild_id, std::string const& payload, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/guilds/" + std::string(guild_id) + "/roles";
	boost::asio::co_spawn(s_.io_context(),
						  execute("POST", path, payload,
								  {{"Authorization", "QQBot " + tok}, {"Content-Type", "application/json"}},
								  "[create role]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

// ─── modify_role ──────────────────────────────────────────────────────────

bool api::modify_role(std::string_view guild_id, std::string_view role_id, std::string const& payload,
					  api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/guilds/" + std::string(guild_id) + "/roles/" + std::string(role_id);

	boost::asio::co_spawn(s_.io_context(),
						  execute("POST", path, payload,
								  {{"Authorization", "QQBot " + tok}, {"Content-Type", "application/json"}},
								  "[modify role]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

// ─── delete_role ──────────────────────────────────────────────────────────

bool api::delete_role(std::string_view guild_id, std::string_view role_id, api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path = "/guilds/" + std::string(guild_id) + "/roles/" + std::string(role_id);

	boost::asio::co_spawn(s_.io_context(),
						  execute("DELETE", path, "", {{"Authorization", "QQBot " + tok}}, "[del role]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

// ─── add_member_role ──────────────────────────────────────────────────────

bool api::add_member_role(std::string_view guild_id, std::string_view user_id, std::string_view role_id,
						  api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path =
		"/guilds/" + std::string(guild_id) + "/members/" + std::string(user_id) + "/roles/" + std::string(role_id);

	boost::asio::co_spawn(s_.io_context(),
						  execute("PUT", path, "{}", {{"Authorization", "QQBot " + tok}}, "[add role]", std::move(cb)),
						  boost::asio::detached);
	return true;
}

// ─── remove_member_role ───────────────────────────────────────────────────

bool api::remove_member_role(std::string_view guild_id, std::string_view user_id, std::string_view role_id,
							 api_callback cb) {
	if (!s_.is_running())
		return false;
	auto tok = get_token();
	if (tok.empty())
		return false;

	std::string path =
		"/guilds/" + std::string(guild_id) + "/members/" + std::string(user_id) + "/roles/" + std::string(role_id);

	boost::asio::co_spawn(
		s_.io_context(),
		execute("DELETE", path, "", {{"Authorization", "QQBot " + tok}}, "[remove role]", std::move(cb)),
		boost::asio::detached);
	return true;
}

} // namespace platform::qq
