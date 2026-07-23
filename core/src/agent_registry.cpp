/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "agent_registry.h"
#include "agent_controller.h"
#include "bot_messaging.h"
#include "chat_storage_sqlite.h"
#include "console_api.h"
#include "log.h"
#include "plugin_manager.h"
#include "self_iteration.h"

#include "platform/qq/session.h"
#include "platform/qq/api.h"

#include <stdexcept>

// ─── qq_adapter (moved here from app/im_adapter.cpp) ───────────────────────
// Thin adapter that converts QQ raw events → message_event and implements
// bot_messaging for the QQ platform.

namespace {

struct qq_adapter final : client::bot_messaging {
	platform::qq::session& session_;
	platform::qq::api api_;
	std::function<void(client::message_event const&)> on_msg_;

	explicit qq_adapter(platform::qq::session& s) : session_(s), api_(s) {}

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

	void wire_events(std::function<void(client::message_event const&)> cb) {
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

} // namespace

// ─── forward declarations (defined in app/llm_adapter.cpp) ─────────────────
std::unique_ptr<client::model_client> make_model_client(client::agent_config const& cfg, bool verify_tls);

namespace {

// ─── self-iteration callback ──────────────────────────────────────────────

static std::function<std::string(bool)> make_si_callback(std::shared_ptr<client::self_iteration_engine> si) {
	if (!si)
		return {};
	return [si](bool dry) -> std::string {
		auto r = dry ? si->dry_run() : si->run();
		if (!r.error.empty())
			return "## 自迭代失败\n\n" + r.error;

		std::ostringstream md;
		md << "## " << (r.dry_run ? "自迭代评估 (dry-run)" : "自迭代完成") << "\n\n";
		md << "| 指标 | 分数 |\n|------|------|\n";
		md << "| 语气 | " << r.avg_tone_score << " |\n";
		md << "| 准确性 | " << r.avg_accuracy_score << " |\n";
		md << "| 完整性 | " << r.avg_completeness_score << " |\n";
		md << "| 效率 | " << r.avg_efficiency_score << " |\n";
		md << "\n**样本**: " << r.samples_evaluated << " | **问题**: " << r.issues_found
		   << " | **改进**: " << r.improvements_applied;
		if (!r.git_commit_hash.empty())
			md << "\n\ncommit: `" << r.git_commit_hash << "`";
		if (!r.summary.empty())
			md << "\n\n" << r.summary;
		return md.str();
	};
}

} // namespace

// ─── agent_registry ────────────────────────────────────────────────────────

client::agent_registry::agent_registry(shared_deps deps) : deps_(std::move(deps)) {}

client::agent_registry::~agent_registry() {
	try {
		stop_all();
	} catch (...) {
	}
}

// ── start_all ──────────────────────────────────────────────────────────────

void client::agent_registry::start_all(bot_config const& cfg) {
	std::lock_guard<std::mutex> lock(mutex_);

	for (auto const& ac : cfg.agents) {
		if (!ac.enabled) {
			log::info("[registry] agent '" + ac.id + "' is disabled — skipping");
			continue;
		}

		auto inst = std::make_unique<agent_instance>();
		inst->config = ac;
		inst->status = agent_status::starting;

		try {
			build_agent(*inst);
			launch_agent(*inst);
			agents_[ac.id] = std::move(inst);
		} catch (std::exception const& ex) {
			log::error("[registry] failed to build agent '" + ac.id + "': " + ex.what());
			inst->status = agent_status::error;
			inst->metrics.last_error = ex.what();
			agents_[ac.id] = std::move(inst);
		}
	}
}

// ── stop_all ───────────────────────────────────────────────────────────────

void client::agent_registry::stop_all() {
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto& [id, inst] : agents_) {
		if (inst->status == agent_status::running || inst->status == agent_status::starting) {
			inst->status = agent_status::stopping;
			try {
				if (inst->alive_flag)
					inst->alive_flag->store(false);
				if (inst->controller)
					inst->controller.reset(); // destructor stops worker pool
				if (inst->im)
					inst->im->stop();
			} catch (std::exception const& ex) {
				log::error("[registry] error stopping agent '" + id + "': " + ex.what());
			}
			inst->status = agent_status::stopped;
		}
	}
}

// ── add_agent ──────────────────────────────────────────────────────────────

void client::agent_registry::add_agent(agent_config cfg) {
	std::lock_guard<std::mutex> lock(mutex_);

	if (cfg.id.empty())
		throw std::runtime_error("agent id must not be empty");
	// Reject invalid characters and path traversal in the ID.
	for (char c : cfg.id) {
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			  (c >= '0' && c <= '9') || c == '_' || c == '-'))
			throw std::runtime_error("agent id contains invalid character: " + std::string(1, c));
	}
	if (cfg.id.find("..") != std::string::npos)
		throw std::runtime_error("agent id must not contain path traversal");
	if (cfg.id.size() > 64)
		throw std::runtime_error("agent id too long (max 64)");
	if (agents_.count(cfg.id))
		throw std::runtime_error("agent '" + cfg.id + "' already exists");

	auto inst = std::make_unique<agent_instance>();
	inst->config = cfg;
	inst->status = agent_status::starting;

	try {
		build_agent(*inst);
		launch_agent(*inst);
		agents_[cfg.id] = std::move(inst);
		log::info("[registry] agent '" + cfg.id + "' added and started");
	} catch (std::exception const& ex) {
		log::error("[registry] failed to build agent '" + cfg.id + "': " + ex.what());
		inst->status = agent_status::error;
		inst->metrics.last_error = ex.what();
		agents_[cfg.id] = std::move(inst);
		throw;
	}
}

// ── remove_agent ───────────────────────────────────────────────────────────

void client::agent_registry::remove_agent(std::string_view id) {
	std::string key(id);
	std::lock_guard<std::mutex> lock(mutex_);

	auto it = agents_.find(key);
	if (it == agents_.end())
		throw std::runtime_error("agent '" + key + "' not found");

	auto& inst = *it->second;
	if (inst.status == agent_status::running || inst.status == agent_status::starting) {
		inst.status = agent_status::stopping;
		if (inst.alive_flag)
			inst.alive_flag->store(false);
		inst.controller.reset();
		inst.im->stop();
		inst.status = agent_status::stopped;
	}

	agents_.erase(it);
	log::info("[registry] agent '" + key + "' removed");
}

// ── start_agent ────────────────────────────────────────────────────────────

void client::agent_registry::start_agent(std::string_view id) {
	std::string key(id);
	std::lock_guard<std::mutex> lock(mutex_);

	auto it = agents_.find(key);
	if (it == agents_.end())
		throw std::runtime_error("agent '" + key + "' not found");

	auto& inst = *it->second;
	if (inst.status == agent_status::running)
		return; // already running

	if (inst.status == agent_status::starting)
		throw std::runtime_error("agent '" + key + "' is already starting");

	inst.status = agent_status::starting;
	inst.metrics.last_error.clear();

	try {
		// Re-create session and controller
		build_agent(inst);
		launch_agent(inst);
		log::info("[registry] agent '" + key + "' started");
	} catch (std::exception const& ex) {
		inst.status = agent_status::error;
		inst.metrics.last_error = ex.what();
		throw;
	}
}

// ── stop_agent ─────────────────────────────────────────────────────────────

void client::agent_registry::stop_agent(std::string_view id) {
	std::string key(id);
	std::lock_guard<std::mutex> lock(mutex_);

	auto it = agents_.find(key);
	if (it == agents_.end())
		throw std::runtime_error("agent '" + key + "' not found");

	auto& inst = *it->second;
	if (inst.status != agent_status::running && inst.status != agent_status::starting)
		return;

	inst.status = agent_status::stopping;
		if (inst.alive_flag)
			inst.alive_flag->store(false);
	inst.controller.reset();
	inst.im->stop();
	inst.status = agent_status::stopped;
	log::info("[registry] agent '" + key + "' stopped");
}

// ── update_agent_config ────────────────────────────────────────────────────

void client::agent_registry::update_agent_config(std::string_view id, agent_config cfg) {
	std::string key(id);
	std::lock_guard<std::mutex> lock(mutex_);

	auto it = agents_.find(key);
	if (it == agents_.end())
		throw std::runtime_error("agent '" + key + "' not found");

	auto& inst = *it->second;
	if (inst.status == agent_status::running || inst.status == agent_status::starting)
		throw std::runtime_error("agent '" + key + "' must be stopped before updating config");

	inst.config = std::move(cfg);
	log::info("[registry] agent '" + key + "' config updated");
}

// ── list_agents ────────────────────────────────────────────────────────────

std::vector<std::pair<std::string, client::agent_status>> client::agent_registry::list_agents() const {
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<std::pair<std::string, agent_status>> out;
	out.reserve(agents_.size());
	for (auto const& [id, inst] : agents_)
		out.emplace_back(id, inst->status);
	return out;
}

// ── get_agent ──────────────────────────────────────────────────────────────

client::agent_instance* client::agent_registry::get_agent(std::string_view id) {
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = agents_.find(std::string(id));
	return (it != agents_.end()) ? it->second.get() : nullptr;
}

// ── count ──────────────────────────────────────────────────────────────────

size_t client::agent_registry::count() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return agents_.size();
}

// ── persist ────────────────────────────────────────────────────────────────

void client::agent_registry::persist(global_config const& global) {
	std::lock_guard<std::mutex> lock(mutex_);

	bot_config cfg;
	cfg.global = global;
	for (auto const& [id, inst] : agents_)
		cfg.agents.push_back(inst->config);

	save_bot_config(deps_.config_path, cfg);
	log::info("[registry] config persisted (" + std::to_string(cfg.agents.size()) + " agents)");
}

// ── build_agent (internal) ─────────────────────────────────────────────────

void client::agent_registry::build_agent(agent_instance& inst) {
	auto const& cfg = inst.config;

	if (cfg.platform == "console") {
		// Console agent: uses console_api instead of a network session.
		// There's no session object; console_api IS the bot_messaging.
		// Main thread drives the read loop — this is handled in main.cpp
		// via run_console_mode() for now.
		throw std::runtime_error("console agents are not supported via registry yet — use --console flag");
	}

	if (cfg.platform != "qq")
		throw std::runtime_error("unsupported platform: '" + cfg.platform + "'");

	// ── QQ session ─────────────────────────────────────────────────────
	auto sess = std::make_unique<platform::qq::session>(cfg.qq_app_id, cfg.qq_app_secret, true);
	// TODO: propagate verify_tls from global_config.

	// ── IM adapter ─────────────────────────────────────────────────────
	auto adapter = std::make_unique<qq_adapter>(*sess);

	// ── LLM client ─────────────────────────────────────────────────────
	auto llm = make_model_client(cfg, true);

	// ── Self-iteration engine ──────────────────────────────────────────
	auto si_db = std::make_shared<client::sqlite_backend>(cfg.storage_dir + "/conversations.db");
	client::self_iteration_config si_cfg;
	si_cfg.enabled = cfg.self_iterate_enabled;
	si_cfg.interval_hours = cfg.self_iterate_interval_hours;
	si_cfg.min_conversations = cfg.self_iterate_min_conversations;
	si_cfg.claude_path = cfg.claude_code_path;
	auto si = std::make_shared<client::self_iteration_engine>(si_cfg, si_db, cfg.workspace);

	// ── Controller ─────────────────────────────────────────────────────
	auto ctrl = std::make_unique<agent_controller>(*adapter, deps_.plugins, std::move(llm), cfg, deps_.reach);
	ctrl->on_self_iterate = make_si_callback(si);

	// ── Wire QQ events → controller ────────────────────────────────────
	adapter->wire_events([ctrl_raw = ctrl.get()](client::message_event const& m) {
		ctrl_raw->handle_message(m);
	});

	// ── Connect handler ────────────────────────────────────────────────
	auto* inst_ptr = &inst;
	sess->on_connect([inst_ptr, id = cfg.id](bool connected, std::string_view reason) {
		std::ostringstream s;
		s << "[agent:" << id << "] " << (connected ? "connected" : "disconnected") << ": " << reason;
		if (connected) {
			client::log::info(s.str());
			std::lock_guard<std::mutex> lk(inst_ptr->mutex);
			inst_ptr->status = agent_status::running;
			inst_ptr->metrics.started_at = std::chrono::system_clock::now();
		} else {
			client::log::warn(s.str());
		}
	});

	// ── Store into instance ────────────────────────────────────────────
	inst.session = std::move(sess);
	inst.im = std::move(adapter);
	// inst.llm left empty; owned by controller
	inst.controller = std::move(ctrl);
	// llm unique_ptr transient — make_model_client result was moved into controller
}

// ── launch_agent (internal) ────────────────────────────────────────────────

void client::agent_registry::launch_agent(agent_instance& inst) {
	if (!inst.session)
		throw std::runtime_error("agent instance has no session");

	inst.session->start();

	// Notify after a short delay to allow connection.
	// Use a weak_ptr to the instance's lifecycle flag — if the agent is
	// stopped/destroyed before the delay elapses, the flag is signaled and
	// we skip notify_startup() to avoid use-after-free.
	inst.alive_flag = std::make_shared<std::atomic<bool>>(true);
	std::weak_ptr<std::atomic<bool>> weak_flag = inst.alive_flag;
	auto* ctrl = inst.controller;
	std::thread([ctrl, weak_flag]() {
		std::this_thread::sleep_for(std::chrono::seconds(3));
		auto flag = weak_flag.lock();
		if (flag && flag->load() && ctrl) {
			try {
				ctrl->notify_startup();
			} catch (...) {
				client::log::warn("[registry] notify_startup failed");
			}
		}
	}).detach();
}
