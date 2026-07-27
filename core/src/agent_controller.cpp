/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "agent_controller.h"
#include "system_command_handler.h"
#include "chat_storage_sqlite.h"
#include "log.h"

#include <future>

namespace {

// ─── magic-number constants ─────────────────────────────────────────────

static constexpr int k_policy_max_turns_per_convo = 50;
static constexpr size_t k_max_tool_content = 4000;
static constexpr int k_safety_limit = 10;

// Truncate at a valid UTF-8 boundary so the appended "[...truncated]"
// marker never splits a multi-byte sequence in half.
static size_t utf8_safe_truncate(std::string_view s, size_t max_len) {
	if (s.size() <= max_len)
		return s.size();
	size_t pos = max_len;
	while (pos > 0) {
		auto c = static_cast<unsigned char>(s[pos]);
		if (c < 0x80 || c >= 0xC0)
			break;
		--pos;
	}
	return pos;
}

static std::shared_ptr<client::chat_storage_backend> make_backend(std::string const& dir) {
	return std::make_shared<client::sqlite_backend>(dir + "/conversations.db");
}

static std::string format_display_content(client::message_event const& msg) {
	std::string content = msg.content;
	if (msg.is_group) {
		std::string nick = msg.sender_nick;
		for (auto& ch : nick)
			if (ch == '[' || ch == ']')
				ch = ' ';
		content = "[" + nick + "]: " + msg.content;
	}
	static auto const kTagRe = [] {
		try {
			return boost::regex(R"(<[^<>]*>)");
		} catch (...) {
			client::log::warn("regex compile failed");
			return boost::regex("", boost::regex::basic);
		}
	}();
	try {
		return boost::regex_replace(content, kTagRe, "");
	} catch (...) {
		return content;
	}
}

} // namespace

// ─── agent_controller ────────────────────────────────────────────────────

client::agent_controller::agent_controller(bot_messaging& bot, plugin_manager& plugins,
										   std::shared_ptr<model_client> llm, agent_config const& config,
		agent_metrics& metrics)
	: bot_(bot), plugins_(plugins), llm_(std::move(llm)), metrics_(metrics),
	  policy_(policy_engine::config{config.max_messages_per_minute, k_policy_max_turns_per_convo,
									config.daily_token_budget}),
	  chat_contexts_(make_backend(config.storage_dir)), storage_dir_(config.storage_dir),
	  admin_ids_(config.admin_user_ids.begin(), config.admin_user_ids.end()), mode_(config.default_mode) {
	try {
		namespace fs = std::filesystem;
		std::string const& ws = config.workspace;

		auto load_system = [&](std::string const& filename) {
			fs::path p = fs::path(ws) / filename;
			if (!fs::exists(p))
				return;
			std::ifstream in(p.string());
			if (!in)
				return;
			std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			if (!content.empty())
				global_system_.push_back({"system", "", content});
		};

		load_system("PROMPT.md");
		load_system("SOUL.md");
		load_system("AGENTS.md");
		load_system("TOOLS.md");
		load_system("MEMORY.md");
		load_system("USER.md");

		// Load group rules template (with {AT_HINT} placeholder for runtime fill)
		{
			fs::path p = fs::path(ws) / "GROUP_RULES.md";
			if (fs::exists(p)) {
				std::ifstream in(p.string());
				if (in)
					group_rules_template_ = std::string(std::istreambuf_iterator<char>(in), {});
			}
		}

		// Register plugin tools
		for (auto const& p : plugins_.plugins())
			tools_.register_provider(p);

		// Register MCP servers
		for (auto const& mcp_cfg : config.mcp_servers) {
			auto mcp = std::make_shared<mcp_client>(mcp_cfg);
			if (mcp->connected()) {
				tools_.register_provider(mcp);
				mcp_clients_.push_back(std::move(mcp));
				log::info("[agent] MCP '" + mcp_cfg.name + "': " +
						  std::to_string(mcp_clients_.back()->tool_count()) + " tools");
			} else {
				log::warn("[agent] MCP '" + mcp_cfg.name + "' failed to connect");
			}
		}

		// Start worker pool
		workers_.start([this](message_event const& m) { process_agent_message(m); },
					   [](message_event const& m) -> std::string { return actor_id_of(m); });
	} catch (std::exception const& ex) {
		log::error("[agent-controller] init failed: " + std::string(ex.what()));
	} catch (...) {
		log::error("[agent-controller] init failed: unknown error");
	}
}

void client::agent_controller::notify_startup() {
	if (admin_ids_.empty())
		return;

	std::string llm_info = llm_ ? (llm_->provider_name() + " / " + llm_->model_name()) : "none";
	std::ostringstream msg;
	msg << "## 绯英 Bot 已启动 ✨\n\n"
		<< "| 项目 | 状态 |\n"
		<< "|------|------|\n"
		<< "| LLM | " << llm_info << " |\n"
		<< "| 模式 | " << to_string(mode_) << " |\n";

	// List connected MCP servers and their tools
	for (auto const& mcp : mcp_clients_) {
		if (mcp->connected())
			msg << "| MCP: " << mcp->server_name() << " | " << mcp->tool_count() << " tools ✅ |\n";
	}

	msg << "\n### 系统命令\n\n"
		<< "| 命令 | 说明 | 权限 |\n"
		<< "|------|------|------|\n"
		<< "| `switch mode` | 查看当前模式 | 所有人 |\n"
		<< "| `switch mode agent/plugin` | 切换模式 | 管理员 |\n"
		<< "| `clear` | 清空当前对话 | 所有人 |\n"
		<< "| `usage` | 查看用量统计 | 所有人 |\n"
		<< "| `delete database` | 删除数据库 | 管理员 |\n"
		<< "| `stop` | 关闭 Bot | 管理员 |\n"
		<< "| `self-iterate` | 自迭代评估+改进 | 管理员 |\n"
		<< "| `self-iterate dry-run` | 仅评估不改 | 管理员 |\n"
		<< "| `help` | 查看帮助 | 所有人 |\n";

	for (auto const& id : admin_ids_)
		bot_.send_private_md(id, msg.str());
}

client::agent_controller::~agent_controller() {
	workers_.stop();
}

void client::agent_controller::handle_message(message_event const& message) {
	std::string block_reason;
	if (!policy_.check_input(message, &block_reason)) {
		log::warn("[policy] input blocked: " + block_reason);
		return;
	}

	std::string n = client::to_ascii_lower(client::trim(message.content));
	system_command_deps cmd_deps{bot_,
								 llm_.get(),
								 chat_contexts_,
								 storage_dir_,
								 admin_ids_,
								 mode_,
								 state_mutex_,
								 [this] { bot_.stop(); },
								 on_self_iterate,
								 [](message_event const& m) -> std::string { return actor_id_of(m); },
								 [this](message_event const& m, std::string_view c) -> bool { return reply_to(m, c); }};
	if (system_command_handler::handle(n, message, cmd_deps))
		return;

	if (plugins_.dispatch_message(bot_, message))
		return;

	runtime_mode mode;
	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		mode = mode_;
	}
	if (mode == runtime_mode::agent) {
		workers_.enqueue(message);
		return;
	}
}

void client::agent_controller::process_agent_message(message_event const& message) {
	metrics_.message_count.fetch_add(1);
	metrics_.last_message_at = std::chrono::system_clock::now();
	auto const t_total = std::chrono::steady_clock::now();
	std::string const cid = [&] {
		auto id = actor_id_of(message);
		return id.empty() ? "global" : id;
	}();

	try {
		if (!llm_ || !llm_->is_enabled()) {
			reply_to(message, "LLM not configured.");
			return;
		}

		auto content = format_display_content(message);
		chat_contexts_.append_user(cid, message.sender_nick, content);
		try {
			chat_contexts_.summarize_with_model(cid, *llm_);
		} catch (...) {
			client::log::warn("[agent_controller] summarize_with_model failed for " + cid);
		}

		auto messages = build_message_list(cid);

		if (message.is_group && !group_rules_template_.empty()) {
			auto hint = message.was_at_mentioned ? "已检测到 @ (被点名)" : "未检测到 @ — 仅在有价值时发言";
			auto rules = group_rules_template_;
			auto pos = rules.find("{AT_HINT}");
			if (pos != std::string::npos)
				rules.replace(pos, kAtHintLen, hint);
			messages.insert(messages.begin() + global_system_.size(), {"system", "", std::move(rules)});
		}

		auto est = token_counter::estimate_tokens(messages);
		int pct = token_counter::default_context_window > 0
					  ? static_cast<int>(est * 100 / token_counter::default_context_window)
					  : 0;
		if (pct >= 50)
			log::warn("[agent] context " + token_counter::ratio_str(est, token_counter::default_context_window));

		std::string limit_reason;
		if (!policy_.check_llm_call(cid, &limit_reason)) {
			reply_to(message, "⚠ " + limit_reason);
			return;
		}
		auto msg_count_before = messages.size();
		tool_loop(messages, cid);

		{
			for (size_t i = msg_count_before; i < messages.size(); ++i) {
				auto const& m = messages[i];
				if (m.role == "assistant" && !m.tool_calls_json.empty()) {
					chat_contexts_.append_assistant_with_tool_calls(cid, m.content, m.tool_calls_json);
				} else if (m.role == "tool") {
					auto capped = m.content.size() > k_max_tool_content
									  ? m.content.substr(0, utf8_safe_truncate(m.content, k_max_tool_content)) +
											"\n[...truncated]"
									  : m.content;
					chat_contexts_.append_tool(cid, m.tool_call_id, std::move(capped));
				}
			}
		}

		std::string final_reply;
		for (auto it = messages.rbegin(); it != messages.rend(); ++it)
			if (it->role == "assistant" && it->tool_calls_json.empty() && !it->content.empty()) {
				final_reply = it->content;
				break;
			}

		if (message.is_group && final_reply.find("[SILENT]") != std::string::npos) {
			log::info("[agent] silent in group, skip reply " +
					  std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
										 std::chrono::steady_clock::now() - t_total)
										 .count()) +
					  "ms convo=" + cid);
			return;
		}

		final_reply = policy_.filter_output(final_reply);
		if (final_reply.empty()) {
			log::warn("[policy] LLM output blocked by filter");
			reply_to(message, "reply blocked");
			return;
		}

		if (!final_reply.empty()) {
			chat_contexts_.append_assistant(cid, final_reply);
			reply_to(message, final_reply);
		}

		log::info("[agent] done " +
				  std::to_string(
					  std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_total)
						  .count()) +
				  "ms convo=" + cid);
	} catch (std::exception const& ex) {
		log::error("[agent] failed: " + std::string(ex.what()));
		reply_to(message, "LLM request failed — check logs for details.");
	}
}

// ─── tool_loop ───────────────────────────────────────────────────────────

void client::agent_controller::tool_loop(std::vector<chat_message>& messages, std::string const& /*convo_id*/) {
	auto const t_start = std::chrono::steady_clock::now();
	int tool_rounds = 0;

	auto tool_schemas = tools_.build_tools_json();
	if (tool_schemas.empty()) {
		// No tools available — simple completion.
		auto content = llm_->complete_messages(messages);
		messages.push_back({"assistant", "", content});
		return;
	}

	for (int iter = 0; iter < k_safety_limit; ++iter) {
		auto t_req = std::chrono::steady_clock::now();
		auto resp = llm_->complete_with_tools(messages, tool_schemas);
		record_token_usage(resp.usage);
		auto ms =
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_req).count();

		if (resp.tool_calls.empty()) {
			// LLM chose to reply to the user — no more tools needed.
			messages.push_back({"assistant", "", resp.content});
			log::info("[agent] done after " + std::to_string(iter) + " rounds, " + std::to_string(ms) + "ms");
			break;
		}

		++tool_rounds;

		nlohmann::json tc_array = nlohmann::json::array();
		for (auto const& tc : resp.tool_calls)
			tc_array.push_back({{"id", tc.id},
								{"type", "function"},
								{"function", {{"name", tc.function_name}, {"arguments", tc.arguments}}}});

		messages.push_back({"assistant", "", "", "", tc_array.dump()});

		// Execute all tool calls in parallel via tool_registry.
		// MCP tools and plugin tools are both registered as tool_providers
		// — no special-casing needed.
		std::weak_ptr<agent_controller> weak_self = shared_from_this();
		std::vector<std::future<std::pair<std::string, std::string>>> futures;
		futures.reserve(resp.tool_calls.size());

		for (auto const& tc : resp.tool_calls) {
			futures.push_back(
				std::async(std::launch::async, [weak_self, tc]() -> std::pair<std::string, std::string> {
					auto self = weak_self.lock();
					if (!self)
						return {tc.id, "(controller destroyed)"};

					auto args = nlohmann::json::parse(tc.arguments, nullptr, false);
					std::string result = self->tools_.execute(tc.function_name, args);
					if (result.empty())
						result = "(no output)";
					return {tc.id, std::move(result)};
				}));
		}

		for (auto& f : futures) {
			auto [id, result] = f.get();
			messages.push_back({"tool", "", std::move(result), id});
		}
	}

	if (tool_rounds > 0) {
		auto total_ms =
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_start).count();
		log::info("[agent] " + std::to_string(tool_rounds) + " tool rounds, " + std::to_string(total_ms) + "ms total");
	}
}

std::vector<client::chat_message> client::agent_controller::build_message_list(std::string const& convo_id) {
	auto messages = chat_contexts_.get_messages(convo_id);
	if (convo_id != "global") {
		for (auto const& gm : global_system_)
			messages.insert(messages.begin(), gm);

		// Inject current date/time so the LLM knows what day/time it is.
		{
			auto now = std::chrono::system_clock::now();
			auto tt = std::chrono::system_clock::to_time_t(now);
			std::tm local_tm{};
#ifdef _WIN32
			localtime_s(&local_tm, &tt);
#else
			localtime_r(&tt, &local_tm);
#endif
			char buf[64];
			std::strftime(buf, sizeof(buf), "%Y年%m月%d日 (%A) %H:%M:%S", &local_tm);
			messages.insert(messages.begin(), {"system", "", std::string("当前时间: ") + buf});
		}
	}
	return messages;
}

bool client::agent_controller::reply_to(message_event const& message, std::string_view content) {
	plugin_context context(bot_, message);
	return context.reply(content);
}

std::string client::agent_controller::actor_id_of(message_event const& message) {
	if (message.is_guild && message.is_private)
		return "dm:" + message.guild_id + ":" + message.sender_id;
	if (message.is_private)
		return "c2c:" + message.user_openid;
	if (message.is_group)
		return "group:" + message.group_id;
	if (message.is_guild)
		return "guild:" + message.guild_id + ":" + message.channel_id;
	return message.sender_id;
}

void client::agent_controller::record_token_usage(nlohmann::json const& usage) {
	if (!llm_)
		return;
	if (usage.is_discarded() || !usage.is_object())
		return;
	int p = usage.value("prompt_tokens", 0);
	int c = usage.value("completion_tokens", 0);
	if (p == 0 && c == 0)
		return;
	chat_contexts_.record_token_usage(llm_->model_name(), p, c);
	metrics_.prompt_tokens.fetch_add(p);
	metrics_.completion_tokens.fetch_add(c);
	log::info("[agent] token: prompt=" + std::to_string(p) + " completion=" + std::to_string(c));
}
