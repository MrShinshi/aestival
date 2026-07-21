/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "platform/qq/dispatch.h"
#include "platform/log.h"

namespace platform::qq {
namespace {

void emit_message(std::string_view event_type, parsed_message&& msg, raw_event_handler const& handler) {
	if (!handler)
		return;
	std::ostringstream s;
	if (msg.is_group)
		s << "[group] " << msg.group_id << "/" << msg.sender_id << ": " << msg.content;
	else if (msg.is_private)
		s << "[private] " << msg.user_openid << ": " << msg.content;
	else if (msg.is_guild)
		s << "[guild] gid=" << msg.guild_id << " ch=" << msg.channel_id << " user=" << msg.sender_nick << ": "
		  << msg.content;
	log::info(s.str());
	handler(event_type, std::move(msg));
}

} // namespace

void dispatch_event(session& s, std::string_view event, std::string_view data) {
	auto const handler = s.get_message_handler();

	// ── C2C private message ────────────────────────────────────────
	if (event == "C2C_MESSAGE_CREATE") {
		try {
			auto p = nlohmann::json::parse(data).get<c2c_message_payload>();
			parsed_message msg;
			msg.is_private = true;
			msg.message_id = std::move(p.id);
			msg.content = std::move(p.content);
			msg.user_openid = std::move(p.user_openid);
			msg.sender_id = msg.user_openid;
			emit_message(event, std::move(msg), handler);
		} catch (std::exception const& e) {
			log::warn(std::string("[dispatch] C2C parse error: ") + e.what());
		} catch (...) {
			log::warn("[dispatch] C2C parse error: unknown");
		}
		return;
	}

	// ── Group AT message (bot was @mentioned) ──────────────────────
	if (event == "GROUP_AT_MESSAGE_CREATE") {
		try {
			auto p = nlohmann::json::parse(data).get<group_message_payload>();
			parsed_message msg;
			msg.is_group = true;
			msg.was_at_mentioned = true;
			msg.message_id = std::move(p.id);
			msg.group_id = std::move(p.group_openid);
			msg.content = std::move(p.content);
			msg.sender_id = std::move(p.member_openid);
			msg.sender_nick = std::move(p.sender_nick);
			emit_message(event, std::move(msg), handler);
		} catch (std::exception const& e) {
			log::warn(std::string("[dispatch] group AT parse error: ") + e.what());
		} catch (...) {
			log::warn("[dispatch] group AT parse error: unknown");
		}
		return;
	}

	// ── Group general message (bot NOT @mentioned) ─────────────────
	if (event == "GROUP_MESSAGE_CREATE") {
		try {
			auto p = nlohmann::json::parse(data).get<group_message_payload>();
			parsed_message msg;
			msg.is_group = true;
			msg.message_id = std::move(p.id);
			msg.group_id = std::move(p.group_openid);
			msg.content = std::move(p.content);
			msg.sender_id = std::move(p.member_openid);
			msg.sender_nick = std::move(p.sender_nick);
			emit_message(event, std::move(msg), handler);
		} catch (std::exception const& e) {
			log::warn(std::string("[dispatch] group parse error: ") + e.what());
		} catch (...) {
			log::warn("[dispatch] group parse error: unknown");
		}
		return;
	}

	// ── Guild channel message ──────────────────────────────────────
	if (event == "PUBLIC_GUILD_MESSAGE_CREATE") {
		try {
			auto p = nlohmann::json::parse(data).get<guild_message_payload>();
			parsed_message msg;
			msg.is_guild = true;
			msg.message_id = std::move(p.id);
			msg.content = std::move(p.content);
			msg.sender_id = std::move(p.user_id);
			msg.sender_nick = std::move(p.username);
			msg.guild_id = std::move(p.guild_id);
			msg.channel_id = std::move(p.channel_id);
			emit_message(event, std::move(msg), handler);
		} catch (std::exception const& e) {
			log::warn(std::string("[dispatch] guild parse error: ") + e.what());
		} catch (...) {
			log::warn("[dispatch] guild parse error: unknown");
		}
		return;
	}

	// ── Guild DM (private channel inside guild) ────────────────────
	// Route: POST /dms/{guild_id}/messages — separate from C2C
	if (event == "DIRECT_MESSAGE_CREATE") {
		try {
			auto p = nlohmann::json::parse(data).get<dm_message_payload>();
			parsed_message msg;
			msg.is_private = true;
			msg.is_guild = true; // DM needs guild-scoped routing
			msg.message_id = std::move(p.id);
			msg.content = std::move(p.content);
			msg.sender_id = std::move(p.user_id);
			msg.user_openid = msg.sender_id; // DM recipient: sender is who we reply to
			msg.guild_id = std::move(p.guild_id);
			emit_message(event, std::move(msg), handler);
		} catch (std::exception const& e) {
			log::warn(std::string("[dispatch] DM parse error: ") + e.what());
		} catch (...) {
			log::warn("[dispatch] DM parse error: unknown");
		}
		return;
	}

	// ── Guild / channel lifecycle events (log only for now) ────────
	if (event == "GUILD_CREATE") {
		log::info(std::string("[guild] GUILD_CREATE ") + std::string(data));
	} else if (event == "CHANNEL_CREATE") {
		log::info(std::string("[guild] CHANNEL_CREATE ") + std::string(data));
	} else if (event == "GUILD_MEMBER_ADD") {
		try {
			auto p = nlohmann::json::parse(data).get<guild_member_payload>();
			std::ostringstream ss;
			ss << "[guild] GUILD_MEMBER_ADD guild=" << p.guild_id << " user=" << p.username << " (" << p.user_id << ")";
			log::info(ss.str());
		} catch (...) {
			log::info("[guild] GUILD_MEMBER_ADD");
		}
	} else if (event == "GUILD_MEMBER_REMOVE") {
		try {
			auto p = nlohmann::json::parse(data).get<guild_member_payload>();
			std::ostringstream ss;
			ss << "[guild] GUILD_MEMBER_REMOVE guild=" << p.guild_id << " user=" << p.user_id;
			log::info(ss.str());
		} catch (...) {
			log::info("[guild] GUILD_MEMBER_REMOVE");
		}
	} else if (event == "INTERACTION_CREATE") {
		log::info(std::string("[interaction] ") + std::string(data));
	} else if (event == "MESSAGE_AUDIT_PASS" || event == "MESSAGE_AUDIT_REJECT") {
		log::debug(std::string("[audit] ") + std::string(event));
	} else if (event == "READY") {
		log::info("[asio] READY!");
	} else {
		std::ostringstream ss;
		ss << "[dispatch] unknown event: " << event;
		log::info(ss.str());
	}
}

} // namespace platform::qq
