/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"

#include "agent_controller.h"
#include "agent_reach_client.h"
#include "agent_reach_plugin.h"
#include "bot_config.h"
#include "chat_storage_sqlite.h"
#include "console_api.h"
#include "log.h"
#include "plugin_manager.h"
#include "self_iteration.h"
#include "simple_test_plugin.h"

#include "platform/qq/session.h"

#include <iostream>

std::unique_ptr<client::model_client> make_model_client(client::bot_config const& cfg);
std::unique_ptr<client::bot_messaging> make_im_adapter(platform::qq::session&, client::bot_config const&);
void wire_qq_events(client::bot_messaging& im, std::function<void(client::message_event const&)> cb);

namespace {

static constexpr auto k_qq_poll_interval = std::chrono::seconds(1);

platform::qq::session* g_bot = nullptr;
std::atomic<bool> g_connected{false};
std::mutex g_signal_mutex;
std::condition_variable g_signal_cv;

void signal_handler(int) {
	client::log::info("Shutting down...");
	if (g_bot)
		g_bot->stop();
}

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

static std::string resolve_workspace(std::string const& exe, std::string const& config_workspace) {
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

// ─── console mode ──────────────────────────────────────────────────────

int run_console_mode(client::bot_config const& config, client::plugin_manager& plugins,
					 std::shared_ptr<client::agent_reach_client> reach_client,
					 std::function<std::string(bool)> on_self_iterate) {
	client::console_api con;
	auto llm = make_model_client(config);
	client::agent_controller controller(con, plugins, std::move(llm), config, reach_client);
	controller.on_self_iterate = on_self_iterate;

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

		controller.handle_message(msg);
		std::cout << "\n";
	}
	return 0;
}

// ─── QQ mode ───────────────────────────────────────────────────────────

int run_qq_mode(client::bot_config const& config, client::plugin_manager& plugins,
				std::shared_ptr<client::agent_reach_client> reach_client,
				std::function<std::string(bool)> on_self_iterate) {
	platform::qq::session bot(config.qq_app_id, config.qq_app_secret, config.verify_tls);
	g_bot = &bot;

	std::signal(SIGINT, signal_handler);
	std::signal(SIGTERM, signal_handler);

	auto im = make_im_adapter(bot, config);
	auto llm = make_model_client(config);

	bot.on_connect([](bool connected, std::string_view reason) {
		std::ostringstream s;
		s << "[main] " << (connected ? "connected" : "disconnected") << ": " << reason;
		if (connected) {
			client::log::info(s.str());
			g_connected = true;
			g_signal_cv.notify_one();
		} else {
			client::log::warn(s.str());
		}
	});

	client::agent_controller controller(*im, plugins, std::move(llm), config, reach_client);
	controller.on_self_iterate = on_self_iterate;

	wire_qq_events(*im, [&controller](client::message_event const& m) { controller.handle_message(m); });

	bot.start();

	client::log::info("Waiting for connection...");
	{
		std::unique_lock<std::mutex> lk(g_signal_mutex);
		g_signal_cv.wait(lk, [] { return g_connected.load() || !g_bot->is_running(); });
	}

	if (!g_bot->is_running()) {
		client::log::error("Failed to start");
		return 1;
	}

	client::log::info("=== Connected! ===");
	controller.notify_startup();

	while (g_bot->is_running())
		std::this_thread::sleep_for(k_qq_poll_interval);

	client::log::info("=== Shutdown complete ===");
	return 0;
}

} // namespace

int main(int argc, char* argv[]) {
	bool console_mode = false;
	for (int i = 1; i < argc; ++i) {
		std::string_view arg(argv[i]);
		if (arg == "--console" || arg == "-c")
			console_mode = true;
	}

	std::string base = exe_dir(argv[0]);
	client::log::info("[main] exe dir: " + base);

	client::bot_config config;
	try {
		config = client::load_bot_config(resolve_path(base, "config/bot_config.json"));
	} catch (std::exception const& ex) {
		client::log::error(std::string("[main] failed to load config: ") + ex.what());
		return 1;
	}

	config.storage_dir = resolve_path(base, config.storage_dir);
	config.workspace = resolve_path(base, config.workspace);
	if (!config.log_file.empty())
		config.log_file = resolve_path(base, config.log_file);

	client::log::init(config.log_file);

	client::plugin_manager plugins;
	plugins.register_plugin(std::make_shared<client::plugins::simple_test_plugin>());

	auto reach = std::make_shared<client::agent_reach_client>(config.verify_tls);
	if (config.agent_reach_enabled)
		plugins.register_plugin(std::make_shared<client::plugins::agent_reach_plugin>(reach));

	auto si_db = std::make_shared<client::sqlite_backend>(config.storage_dir + "/conversations.db");
	client::self_iteration_config si_cfg;
	si_cfg.enabled = config.self_iterate_enabled;
	si_cfg.interval_hours = config.self_iterate_interval_hours;
	si_cfg.min_conversations = config.self_iterate_min_conversations;
	si_cfg.claude_path = config.claude_code_path;

	auto si = std::make_shared<client::self_iteration_engine>(si_cfg, si_db, resolve_workspace(base, config.workspace));

	auto on_si = [si](bool dry) -> std::string {
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

	if (console_mode)
		return run_console_mode(config, plugins, reach, on_si);
	else
		return run_qq_mode(config, plugins, reach, on_si);
}
