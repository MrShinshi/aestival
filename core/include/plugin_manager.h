/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "log.h"
#include "plugin.h"
#include "plugin_config_backend.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
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

	// ── Config backend ──────────────────────────────────────────────────

	void set_config_backend(std::shared_ptr<plugin_config_backend> backend) {
		std::lock_guard<std::mutex> lock(config_mutex_);
		config_backend_ = std::move(backend);
	}

	// ── Per-agent enable/disable (write-through to SQLite + memory cache) ──

	bool is_plugin_enabled(std::string_view plugin_name, std::string const& agent_id) const {
		std::lock_guard<std::mutex> lock(config_mutex_);
		// Check memory cache first (fast path — disabled plugins are cached).
		auto key = cache_key(agent_id, plugin_name);
		if (disabled_cache_.count(key))
			return false;
		// If not in cache and no backend, default to enabled.
		if (!config_backend_)
			return true;
		bool enabled = config_backend_->is_plugin_enabled_for_agent(agent_id, std::string(plugin_name));
		if (!enabled)
			disabled_cache_.insert(key); // cache for future lookups
		return enabled;
	}

	bool is_plugin_enabled_for_user(std::string_view plugin_name, std::string const& user_id) const {
		std::lock_guard<std::mutex> lock(config_mutex_);
		if (!config_backend_)
			return true;
		return config_backend_->is_plugin_enabled_for_user(user_id, std::string(plugin_name));
	}

	void enable_plugin_for_agent(std::string_view plugin_name, std::string const& agent_id) {
		std::lock_guard<std::mutex> lock(config_mutex_);
		auto key = cache_key(agent_id, plugin_name);
		disabled_cache_.erase(key);
		if (config_backend_)
			config_backend_->set_plugin_enabled_for_agent(agent_id, std::string(plugin_name), true);
	}

	void disable_plugin_for_agent(std::string_view plugin_name, std::string const& agent_id) {
		std::lock_guard<std::mutex> lock(config_mutex_);
		auto key = cache_key(agent_id, plugin_name);
		disabled_cache_.insert(key);
		if (config_backend_)
			config_backend_->set_plugin_enabled_for_agent(agent_id, std::string(plugin_name), false);
	}

	// ── List plugins with status for an agent ───────────────────────────

	std::vector<std::tuple<std::string, std::string, bool, std::string>>
	list_plugins(std::string const& agent_id) const {
		std::vector<std::tuple<std::string, std::string, bool, std::string>> result;
		for (auto const& p : plugins_) {
			if (!p)
				continue;
			auto desc = p->descriptor();
			bool enabled = is_plugin_enabled(p->name(), agent_id);
			result.emplace_back(desc.name, desc.display_name, enabled, desc.description);
		}
		return result;
	}

	// ── Get raw plugin pointer by name ──────────────────────────────────

	plugin* find_plugin(std::string_view name) const {
		for (auto const& p : plugins_) {
			if (p && p->name() == name)
				return p.get();
		}
		return nullptr;
	}

	// ── Message dispatch (with per-agent and per-user filtering) ────────

	bool dispatch_message(bot_messaging& bot, const message_event& message,
						  std::string const& agent_id = "") {
		bool handled_any = false;
		std::string user_id = derive_user_id(message);

		for (const auto& current : plugins_) {
			if (!current || !current->can_handle(message))
				continue;

			// Per-agent filter: skip if disabled for this agent.
			if (!agent_id.empty() && !is_plugin_enabled(current->name(), agent_id))
				continue;

			// Per-user filter: skip if disabled for this user.
			if (!user_id.empty() && !is_plugin_enabled_for_user(current->name(), user_id))
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
	static std::string cache_key(std::string_view agent, std::string_view plugin) {
		std::string k;
		k.reserve(agent.size() + 2 + plugin.size());
		k.append(agent);
		k.append("::");
		k.append(plugin);
		return k;
	}

	static std::string derive_user_id(message_event const& msg) {
		if (!msg.user_openid.empty())
			return msg.user_openid;
		if (!msg.sender_id.empty())
			return msg.sender_id;
		return std::string(msg.group_id); // fallback: group scope
	}

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
	std::shared_ptr<plugin_config_backend> config_backend_;
	mutable std::unordered_set<std::string> disabled_cache_;
	mutable std::mutex config_mutex_;
};

} // namespace client
