/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"

#include "agent_controller.h"
#include "agent_instance.h"
#include "agent_reach_client.h"
#include "agent_reach_plugin.h"
#include "agent_registry.h"
#include "bot_config.h"
#include "chat_storage_sqlite.h"
#include "console_api.h"
#include "log.h"
#include "management_api.h"
#include "plugin_manager.h"
#include "self_iteration.h"
#include "simple_test_plugin.h"

#include <iostream>

// ─── LLM factory (defined in app/llm_adapter.cpp) ──────────────────────────
std::unique_ptr<client::model_client> make_model_client(client::agent_config const& cfg, bool verify_tls);

namespace {

static constexpr auto k_qq_poll_interval = std::chrono::seconds(1);

// ── path helpers ───────────────────────────────────────────────────────────

static std::string exe_dir(char const* argv0) {
	std::filesystem::path p(argv0);
	if (!p.is_absolute())
		p = std::filesystem::current_path() / p;
	std::error_code ec;
	auto canonical = std::filesystem::canonical(p, ec);
	if (!ec)
		p = canonical;
	return p.parent_path().string();
}

static std::string resolve_workspace(std::string const& /*exe*/, std::string const& config_workspace) {
	if (auto* env = std::getenv("QCLAW_WORKSPACE"))
		return std::string(env);
	if (!config_workspace.empty())
		return config_workspace;
#ifdef _WIN32
	if (auto* home = std::getenv("USERPROFILE"))
		return std::string(home);
	if (auto* d = std::getenv("HOMEDRIVE"))
		if (auto* p = std::getenv("HOMEPATH"))
			return std::string(d) + p;
#else
	if (auto* home = std::getenv("HOME"))
		return std::string(home);
#endif
	return ".";
}

static std::string resolve_path(std::string const& base_dir, std::string const& p) {
	if (p.empty())
		return p;
	std::filesystem::path fp(p);
	if (fp.is_absolute())
		return p;
	return (std::filesystem::path(base_dir) / fp).lexically_normal().string();
}

// ── console mode ───────────────────────────────────────────────────────────

int run_console_mode(client::agent_config const& config, client::plugin_manager& plugins,
					 std::shared_ptr<client::agent_reach_client> reach_client,
					 std::function<std::string(bool)> on_self_iterate, bool verify_tls) {
	client::console_api con;
	auto llm = std::shared_ptr<client::model_client>(make_model_client(config, verify_tls));
	auto ctrl = std::make_shared<client::agent_controller>(con, plugins, llm, config, reach_client);
	ctrl->on_self_iterate = on_self_iterate;

	std::cerr << "=== aestival console mode ===\n"
			  << "Type messages; 'exit' or Ctrl+C to quit.\n\n"
			  << std::flush;

	std::string line;
	while (!con.is_stopped() && std::getline(std::cin, line)) {
		if (line == "exit" || line == "quit")
			break;
		if (line.empty())
			continue;

		client::message_event msg;
		msg.protocol = "console";
		msg.content = std::move(line);
		msg.is_private = true;
		msg.user_openid = "console";
		msg.sender_id = "console";
		msg.sender_nick = "console";

		ctrl->handle_message(msg);
		std::cout << "\n";
	}
	return 0;
}

} // namespace

// ─── main ──────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
	bool console_mode = false;
	for (int i = 1; i < argc; ++i) {
		std::string_view arg(argv[i]);
		if (arg == "--console" || arg == "-c")
			console_mode = true;
	}

	std::string base = exe_dir(argv[0]);
	client::log::info("[main] exe dir: " + base);

	// ── load config ──────────────────────────────────────────────────────
	client::bot_config cfg;
	try {
		cfg = client::load_bot_config(resolve_path(base, "config/bot_config.json"));
	} catch (std::exception const& ex) {
		client::log::error(std::string("[main] failed to load config: ") + ex.what());
		return 1;
	}

	// Resolve paths relative to exe dir
	for (auto& a : cfg.agents) {
		a.storage_dir = resolve_path(base, a.storage_dir);
		a.workspace = resolve_path(base, a.workspace);
	}
	if (!cfg.global.log_file.empty())
		cfg.global.log_file = resolve_path(base, cfg.global.log_file);

	client::log::init(cfg.global.log_file);

	// ── shared dependencies ──────────────────────────────────────────────
	client::plugin_manager plugins;
	plugins.register_plugin(std::make_shared<client::plugins::simple_test_plugin>());

	auto reach = std::make_shared<client::agent_reach_client>(cfg.global.verify_tls);

	// ── console mode (legacy, bypasses registry) ─────────────────────────
	if (console_mode) {
		// Pick the first agent config for console mode, or a default.
		client::agent_config const& ac = cfg.agents.empty() ? client::agent_config{} : cfg.agents[0];
		if (ac.agent_reach_enabled)
			plugins.register_plugin(std::make_shared<client::plugins::agent_reach_plugin>(reach));

		auto si_db = std::make_shared<client::sqlite_backend>(ac.storage_dir + "/conversations.db");
		client::self_iteration_config si_cfg;
		si_cfg.enabled = ac.self_iterate_enabled;
		si_cfg.interval_hours = ac.self_iterate_interval_hours;
		si_cfg.min_conversations = ac.self_iterate_min_conversations;
		si_cfg.claude_path = ac.claude_code_path;
		auto si = std::make_shared<client::self_iteration_engine>(si_cfg, si_db,
																   resolve_workspace(base, ac.workspace));

		return run_console_mode(ac, plugins, reach, client::make_si_callback(si), cfg.global.verify_tls);
	}

	// ── QQ / multi-agent mode ────────────────────────────────────────────
	client::agent_registry::shared_deps deps{plugins, reach, resolve_path(base, "config/bot_config.json")};
	client::agent_registry registry(deps);

	// For each agent that has agent_reach_enabled, register the search plugin once.
	// (agent_reach_plugin is stateless; registering it per agent is harmless.)
	for (auto const& a : cfg.agents) {
		if (a.agent_reach_enabled) {
			plugins.register_plugin(std::make_shared<client::plugins::agent_reach_plugin>(reach));
			break; // only need to register once
		}
	}

	// Handle shutdown signals.
	// Must be a static atomic: signal handlers accept only function pointers
	// (no captures), but a captureless lambda can access a static.
	static std::atomic<bool> s_shutdown{false};
	s_shutdown.store(false);
	std::signal(SIGINT,  [](int) { s_shutdown.store(true); });
	std::signal(SIGTERM, [](int) { s_shutdown.store(true); });

	registry.on_agent_startup = [](std::string_view agent_id, bool connected) {
		if (connected)
			client::log::info("[main] agent '" + std::string(agent_id) + "' started successfully");
		else
			client::log::warn("[main] agent '" + std::string(agent_id) + "' failed to connect");
	};

	try {
		registry.start_all(cfg);
	} catch (std::exception const& ex) {
		client::log::error(std::string("[main] agent startup failed: ") + ex.what());
		return 1;
	}

	// ── Management API (Phase 2) ─────────────────────────────────────────
	client::management_api mgmt_api(registry, cfg.global);
	if (cfg.global.management_api_enabled && !cfg.global.jwt_secret.empty()) {
		try {
			mgmt_api.start();
			client::log::info("[main] management API started on " + cfg.global.management_listen);
		} catch (std::exception const& ex) {
			client::log::warn(std::string("[main] management API failed to start: ") + ex.what());
		}
	}

	client::log::info("=== aestival running (" + std::to_string(registry.count()) + " agents) ===");

	// Main loop: poll until all agents are stopped or shutdown signal received.
	while (!s_shutdown.load()) {
		auto agents = registry.list_agents();
		bool any_running = false;
		for (auto const& [id, status] : agents) {
			if (status == client::agent_status::running || status == client::agent_status::starting) {
				any_running = true;
				break;
			}
		}
		if (!any_running)
			break;
		std::this_thread::sleep_for(k_qq_poll_interval);
	}

	if (s_shutdown.load())
		client::log::info("=== Shutdown signal received ===");
	mgmt_api.stop();
	registry.stop_all();
	client::log::info("=== Shutdown complete ===");
	return 0;
}
