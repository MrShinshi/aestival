/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "log.h"
#include "plugin.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace client {

struct plugin_manager {
	bool register_plugin(std::shared_ptr<plugin> value) {
		if (!value)
			return false;

		auto duplicate = std::find_if(plugins_.begin(), plugins_.end(),
									  [&](const auto& current) { return current && current->name() == value->name(); });
		if (duplicate != plugins_.end()) {
			std::ostringstream s;
			s << "[plugin-manager] Duplicate plugin ignored: " << value->name();
			log::warn(s.str());
			return false;
		}

		plugins_.push_back(std::move(value));
		std::stable_sort(plugins_.begin(), plugins_.end(),
						 [](const auto& lhs, const auto& rhs) { return lhs->priority() > rhs->priority(); });
		return true;
	}

	bool dispatch_message(bot_messaging& bot, const message_event& message) {
		bool handled_any = false;
		for (const auto& current : plugins_) {
			if (!current || !current->can_handle(message))
				continue;

			plugin_context context(bot, message);
			try {
				auto result = current->handle(context);
				log_receipt(*current, context);
				if (context.stop_requested())
					honor_stop_request(*current, bot);
				handled_any = handled_any || result.handled;
				if (result.stop_processing)
					break;
			} catch (const std::exception& ex) {
				std::ostringstream s;
				s << "[plugin-manager] Plugin " << current->name() << " failed: " << ex.what();
				log::error(s.str());
			} catch (...) {
				std::ostringstream s;
				s << "[plugin-manager] Plugin " << current->name() << " failed: unknown error";
				log::error(s.str());
			}
		}
		return handled_any;
	}

	// P2-2: expose plugins for tool registration
	std::vector<std::shared_ptr<plugin>> const& plugins() const {
		return plugins_;
	}

	private:
	static void honor_stop_request(const plugin& current, bot_messaging& bot) {
		if (!has_capability(current.capabilities(), plugin_capability::request_stop)) {
			std::ostringstream s;
			s << "[plugin-manager] Plugin " << current.name() << " requested stop without capability";
			log::warn(s.str());
			return;
		}
		std::ostringstream s;
		s << "[plugin-manager] Stop requested by plugin: " << current.name();
		log::warn(s.str());
		bot.stop();
	}

	static void log_receipt(const plugin& current, const plugin_context& context) {
		const auto& receipt = context.last_receipt();
		if (!receipt.attempted)
			return;
		std::ostringstream s;
		s << "[plugin:" << current.name() << "] reply " << (receipt.delivered ? "ok" : "failed") << ": "
		  << receipt.detail;
		if (receipt.delivered)
			log::info(s.str());
		else
			log::warn(s.str());
	}

	std::vector<std::shared_ptr<plugin>> plugins_;
};

} // namespace client
