/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "bot_messaging.h"
#include "log.h"
#include "message_types.h"
#include "platform/qq/session.h"
#include "platform/qq/api.h"
#include "bot_config.h"

#include <memory>

struct qq_adapter : client::bot_messaging {
	platform::qq::session& session_;
	platform::qq::api api_;
	std::function<void(client::message_event const&)> on_msg_;

	explicit qq_adapter(platform::qq::session& s) : session_(s), api_(s) {
	}

	void stop() override {
		api_.stop();
		session_.stop();
	}

	bool send_private_message(std::string_view o, std::string_view c) override {
		return api_.send_private_message(o, c, {});
	}
	bool send_private_md(std::string_view o, std::string_view c) override {
		return api_.send_private_md(o, c, {});
	}
	bool send_group_md(std::string_view g, std::string_view c) override {
		return api_.send_group_md(g, c, {});
	}
	bool send_channel_md(std::string_view ch, std::string_view c) override {
		return api_.send_channel_md(ch, c, {});
	}
	bool send_dms_md(std::string_view g, std::string_view c) override {
		return api_.send_dms_md(g, c, {});
	}

	void set_callback(std::function<void(client::message_event const&)> cb) {
		on_msg_ = std::move(cb);
		session_.on_message([this](std::string_view, platform::qq::parsed_message&& pm) {
			if (!on_msg_)
				return;
			client::message_event msg;
			msg.message_id = std::move(pm.message_id);
			msg.content = std::move(pm.content);
			msg.sender_id = std::move(pm.sender_id);
			msg.sender_nick = std::move(pm.sender_nick);
			msg.user_openid = std::move(pm.user_openid);
			msg.group_id = std::move(pm.group_id);
			msg.channel_id = std::move(pm.channel_id);
			msg.guild_id = std::move(pm.guild_id);
			msg.is_private = pm.is_private;
			msg.is_group = pm.is_group;
			msg.is_guild = pm.is_guild;
			msg.was_at_mentioned = pm.was_at_mentioned;
			msg.protocol = "qq";
			on_msg_(msg);
		});
	}
};

std::unique_ptr<client::bot_messaging> make_im_adapter(platform::qq::session& s, client::bot_config const&) {
	return std::make_unique<qq_adapter>(s);
}

void wire_qq_events(client::bot_messaging& im, std::function<void(client::message_event const&)> cb) {
	auto* qa = dynamic_cast<qq_adapter*>(&im);
	if (qa)
		qa->set_callback(std::move(cb));
	else
		client::log::warn("[im-adapter] wire_qq_events: IM backend is not qq_adapter, events not wired");
}
