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

static constexpr int k_default_search_result_count = 5;
static constexpr int k_default_bili_hot_count = 10;
static constexpr int k_policy_max_turns_per_convo = 50;
static constexpr size_t k_max_tool_content = 4000;

// Truncate at a valid UTF-8 boundary so the appended "[...truncated]"
// marker never splits a multi-byte sequence in half.
static size_t utf8_safe_truncate(std::string_view s, size_t max_len) {
	if (s.size() <= max_len)
		return s.size();
	size_t pos = max_len;
	while (pos > 0) {
		auto c = static_cast<unsigned char>(s[pos]);
		// Lead byte (>=0xC0) or ASCII (<0x80) — valid boundary.
		if (c < 0x80 || c >= 0xC0)
			break;
		--pos;
	}
	return pos;
}
static constexpr int k_safety_limit = 10;

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
										   std::shared_ptr<agent_reach_client> reach)
	: bot_(bot), plugins_(plugins), llm_(std::move(llm)), reach_(std::move(reach)),
	  policy_(policy_engine::config{config.max_messages_per_minute, k_policy_max_turns_per_convo,
									config.daily_token_budget}),
	  chat_contexts_(make_backend(config.storage_dir)), storage_dir_(config.storage_dir),
	  admin_ids_(config.admin_user_ids.begin(), config.admin_user_ids.end()), mode_(config.default_mode) {
	if (!reach_)
		log::error("[agent-controller] no agent_reach_client");

	try {
		namespace fs = std::filesystem;
		std::string const& ws = config.workspace; // already resolved by main

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

		if (reach_) {
			auto const& cap = reach_->capabilities();
			std::ostringstream info;
			info << "system tools:"
				 << " curl=" << (cap.web_fetch ? "ok" : "no") << " bili=" << (cap.bilibili ? "ok" : "no")
				 << " gh=" << (cap.github ? "ok" : "no") << " twitter=" << (cap.twitter ? "ok" : "no")
				 << " mcporter=" << (cap.exa_search ? "ok" : "no");
			global_system_.push_back({"system", "", info.str()});
		}

		// P2-2: register plugin tools
		for (auto const& p : plugins_.plugins())
			tools_.register_provider(p);

		// P2-3: start worker pool
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
	auto const& cap = reach_ ? reach_->capabilities() : agent_reach_capabilities{};
	std::string llm_info = llm_ ? (llm_->provider_name() + " / " + llm_->model_name()) : "none";
	std::ostringstream msg;
	msg << "## 绯英 Bot 已启动 ✨\n\n"
		<< "| 项目 | 状态 |\n"
		<< "|------|------|\n"
		<< "| LLM | " << llm_info << " |\n"
		<< "| 模式 | " << to_string(mode_) << " |\n"
		<< "| curl | " << (cap.web_fetch ? "✅" : "❌") << " |\n"
		<< "| bili | " << (cap.bilibili ? "✅" : "❌") << " |\n"
		<< "| gh   | " << (cap.github ? "✅" : "❌") << " |\n"
		<< "| twitter | " << (cap.twitter ? "✅" : "❌") << " |\n"
		<< "| mcporter | " << (cap.exa_search ? "✅" : "❌") << " |\n"
		<< "\n### 系统命令\n\n"
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
		<< "\n### 快捷搜索命令\n\n"
		<< "| 命令 | 说明 |\n"
		<< "|------|------|\n"
		<< "| `/bilibili <关键词>` | B站搜索 |\n"
		<< "| `/github <关键词>` | GitHub 搜索 |\n"
		<< "| `/v2ex <关键词>` | V2EX 搜索 |\n"
		<< "| `/twitter <关键词>` | Twitter 搜索 |\n"
		<< "| `fetch <URL>` | 抓取网页全文 |\n"
		<< "| `help` | 查看帮助 |\n"
		<< "\n以上命令跳过 AI, 直接执行。自然语言聊天自动进入 AI 模式。";
	for (auto const& id : admin_ids_)
		bot_.send_private_md(id, msg.str());
}

client::agent_controller::~agent_controller() {
	workers_.stop();
}

void client::agent_controller::handle_message(message_event const& message) {
	// P2-1: pre-input policy check (prompt injection, spam)
	std::string block_reason;
	if (!policy_.check_input(message, &block_reason)) {
		log::warn("[policy] input blocked: " + block_reason);
		return;
	}

	// P2-3: delegate system commands
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

		// Inject group silence rules from GROUP_RULES.md template
		// with {AT_HINT} replaced by actual @mention status.
		if (message.is_group && !group_rules_template_.empty()) {
			auto hint = message.was_at_mentioned ? "已检测到 @ (被点名)" : "未检测到 @ — 仅在有价值时发言";
			auto rules = group_rules_template_;
			auto pos = rules.find("{AT_HINT}");
			if (pos != std::string::npos)
				rules.replace(pos, kAtHintLen, hint);
			messages.insert(messages.begin() + global_system_.size(), {"system", "", std::move(rules)});
		}

		// P3-1: token estimate for context window awareness
		auto est = token_counter::estimate_tokens(messages);
		int pct = token_counter::default_context_window > 0
					  ? static_cast<int>(est * 100 / token_counter::default_context_window)
					  : 0;
		if (pct >= 50)
			log::warn("[agent] context " + token_counter::ratio_str(est, token_counter::default_context_window));

		// P2-1: pre-LLM rate limit + budget check
		std::string limit_reason;
		if (!policy_.check_llm_call(cid, &limit_reason)) {
			reply_to(message, "⚠ " + limit_reason);
			return;
		}
		auto msg_count_before = messages.size();
		tool_loop(messages, cid);

		// Persist tool messages so they survive across turns.
		// Without this, next turn's LLM won't see the raw tool output
		// and may hallucinate data (e.g. fabricating pagination results).
		{
			for (size_t i = msg_count_before; i < messages.size(); ++i) {
				auto const& m = messages[i];
				if (m.role == "assistant" && !m.tool_calls_json.empty()) {
					chat_contexts_.append_assistant_with_tool_calls(cid, m.content, m.tool_calls_json);
				} else if (m.role == "tool") {
					auto capped = m.content.size() > k_max_tool_content
									  ? m.content.substr(0, utf8_safe_truncate(m.content, k_max_tool_content)) + "\n[...truncated]"
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

		// [SILENT]: LLM chose not to reply in group — don't send,
		// don't save to history (but user message is already saved).
		if (message.is_group && final_reply.find("[SILENT]") != std::string::npos) {
			log::info("[agent] silent in group, skip reply " +
					  std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
										 std::chrono::steady_clock::now() - t_total)
										 .count()) +
					  "ms convo=" + cid);
			return;
		}

		// P2-1: post-LLM output filter
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

// ─── shell tool schema ───────────────────────────────────────────────────

nlohmann::json client::agent_controller::build_tools() {
	std::call_once(tools_cache_flag_, [this] {
		auto fn = nlohmann::json::object();
		fn["name"] = "shell";
		fn["description"] = "Execute a shell command and return stdout. "
							"Available: curl, bili, gh, twitter, mcporter. "
							"B站个人数据: bili following/favorites/history/whoami. "
							"See TOOLS.md for exact command formats.\n"
							"CRITICAL ANTI-HALLUCINATION RULES:\n"
							"- NEVER fabricate or guess data. EVERY fact MUST come from actual tool output.\n"
							"- When user says '继续/还有/下一页/剩下的/需要': you MUST re-run the SAME tool command. "
							"DO NOT guess or complete the list from memory.\n"
							"- BAD example: bili following returned 31 items, you showed 20, user says '继续' "
							"-> you make up 11 names. THIS IS WRONG! "
							"CORRECT: call bili following again.\n"
							"- If you cannot get more data, honestly tell the user '工具只返回了这些'.";
		fn["parameters"] = {
			{"type", "object"},
			{"properties",
			 {{"command",
			   {{"type", "string"},
				{"description", "The shell command. Examples: "
								"'bili hot -n 5', "
								"'curl -s --max-time 15 \"https://wttr.in/Beijing?lang=zh&format=3\"', "
								"'gh search repos \"q\" --sort stars --limit 3 --json name,stargazersCount,url'"}}}}},
			{"required", nlohmann::json::array({"command"})}};

		auto tool = nlohmann::json::object();
		tool["type"] = "function";
		tool["function"] = std::move(fn);
		auto result = nlohmann::json::array({std::move(tool)});

		// P2-2: append plugin-provided tools
		auto plugin_tools = tools_.build_tools_json();
		for (auto const& pt : plugin_tools)
			result.push_back(pt);

		cached_tools_json_ = result;
	});

	return cached_tools_json_;
}

bool client::agent_controller::is_safe_command(std::string_view cmd) const {
	auto c = client::trim(std::string(cmd));
	if (c.empty())
		return false;
	static const char* kSafe[] = {"curl ", "bili ", "gh ", "twitter ", "mcporter "};
	for (auto p : kSafe)
		if (client::starts_with(c, p))
			return true;
	return false;
}

// ─── is_safe_curl_target ─────────────────────────────────────────────────
// Reject curl commands that target localhost, private IPs, cloud metadata
// endpoints, or other sensitive internal destinations.

static bool is_safe_curl_target(std::string_view cmd) {
	// Extract URL from curl command -- look for the first http/https argument.
	auto pos = cmd.find("http://");
	if (pos == std::string::npos)
		pos = cmd.find("https://");
	if (pos == std::string::npos)
		return true; // no URL, let through (curl will fail)

	auto url = cmd.substr(pos);
	auto end = url.find_first_of(" \t\r\n\"");
	std::string host;
	if (end != std::string::npos)
		url = url.substr(0, end);

	// Strip scheme
	auto scheme_end = url.find("://");
	if (scheme_end != std::string::npos)
		url = url.substr(scheme_end + 3);

	// Strip path/port -- keep only host
	auto slash = url.find('/');
	auto colon = url.find(':');
	if (slash != std::string::npos)
		url = url.substr(0, slash);
	if (colon != std::string::npos && colon < (slash != std::string::npos ? slash : url.size())) {
		// This is a port, not an IPv6 colon -- strip port
		auto port_part = url.substr(colon);
		if (port_part.find(':') == std::string::npos) // single colon = port
			url = url.substr(0, colon);
	}

	host = std::string(url);

	// Reject localhost / loopback (IPv4)
	if (host == "localhost" || host == "127.0.0.1" || host == "0.0.0.0")
		return false;

	// Reject IPv6 loopback / unspecified / IPv4-mapped
	if (host == "::1" || host == "::" || client::starts_with(host, "[::1]") || client::starts_with(host, "[::ffff:") ||
		client::starts_with(host, "[::]"))
		return false;

	// Reject IPv6 link-local (fe80::) and unique-local (fc00::/7)
	if (client::starts_with(host, "[fe80") || client::starts_with(host, "[fc") || client::starts_with(host, "[fd"))
		return false;

	// Reject private IPs
	if (client::starts_with(host, "10.") || client::starts_with(host, "192.168."))
		return false;
	if (client::starts_with(host, "172.")) {
		auto dot = host.find('.', 4);
		if (dot != std::string::npos) {
			try {
				int b = std::stoi(std::string(host.substr(4, dot - 4)));
				if (b >= 16 && b <= 31)
					return false;
			} catch (...) {
			}
		}
	}

	// Reject link-local (169.254.x.x) -- cloud metadata, zeroconf
	if (client::starts_with(host, "169.254."))
		return false;

	return true;
}

// ─── record_token_usage ──────────────────────────────────────────────────

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
	log::info("[agent] token: prompt=" + std::to_string(p) + " completion=" + std::to_string(c) +
			  " total=" + std::to_string(p + c));
}

// ─── route_shell_command ─────────────────────────────────────────────────
// Parses a shell command string and routes to the appropriate dedicated
// parser method on agent_reach_client.  Dedicated parsers provide
// formatted output (trimmed, line-numbered, emoji-bulleted), length
// clipping, structured JSON parsing, and correct timeouts per backend.
//
// Commands NOT matched by any parser fall through to raw exec().

std::string client::agent_controller::route_shell_command(std::string_view cmd) const {
	if (!reach_)
		return "(agent-reach unavailable)";

	auto c = client::trim(std::string(cmd));

	// ── bili ────────────────────────────────────────────────────────────
	if (client::starts_with(c, "bili ")) {
		// bili search "X" --type video -n N
		{
			static auto const kRe = [] {
				try {
					return boost::regex(R"(bili\s+search\s+\"([^\"]*)\"(?:\s+--type\s+\w+)?(?:\s+-n\s+(\d+))?)",
										boost::regex::icase);
				} catch (...) {
					client::log::warn("regex compile failed");
					return boost::regex("");
				}
			}();
			boost::smatch m;
			if (boost::regex_search(c, m, kRe) && m[1].matched) {
				int n = k_default_search_result_count;
				if (m[2].matched)
					try {
						n = std::stoi(m[2].str());
					} catch (...) {
					}
				return reach_->search_bilibili(m[1].str(), n);
			}
		}

		// bili hot -n N
		{
			static auto const kRe = [] {
				try {
					return boost::regex(R"(bili\s+hot(?:\s+-n\s+(\d+))?)", boost::regex::icase);
				} catch (...) {
					client::log::warn("regex compile failed");
					return boost::regex("");
				}
			}();
			boost::smatch m;
			if (boost::regex_search(c, m, kRe)) {
				int n = k_default_bili_hot_count;
				if (m[1].matched)
					try {
						n = std::stoi(m[1].str());
					} catch (...) {
					}
				return reach_->bili_hot(n);
			}
		}

		// Other bili (following, favorites, history, user, user-videos, whoami, rank)
		return reach_->exec(c);
	}

	// ── curl ────────────────────────────────────────────────────────────
	if (client::starts_with(c, "curl ")) {
		// V2EX hot topics
		if (c.find("v2ex.com") != std::string::npos && c.find("hot.json") != std::string::npos) {
			return reach_->search_v2ex("");
		}
		// V2EX node API
		if (c.find("v2ex.com") != std::string::npos && c.find("show.json?node_name=") != std::string::npos) {
			static auto const kRe = [] {
				try {
					return boost::regex(R"(node_name=([^&\"]+))");
				} catch (...) {
					client::log::warn("regex compile failed");
					return boost::regex("");
				}
			}();
			boost::smatch m;
			std::string node;
			if (boost::regex_search(c, m, kRe) && m[1].matched)
				node = m[1].str();
			return reach_->search_v2ex(node);
		}
		// Jina Reader (fetch page via r.jina.ai)
		if (c.find("r.jina.ai/") != std::string::npos) {
			static auto const kRe = [] {
				try {
					return boost::regex(R"(r\.jina\.ai/(\S+?)(?:\"|\s|$))");
				} catch (...) {
					client::log::warn("regex compile failed");
					return boost::regex("");
				}
			}();
			boost::smatch m;
			if (boost::regex_search(c, m, kRe) && m[1].matched) {
				auto url = m[1].str();
				if (!url.empty() && url.back() == '"')
					url.pop_back();
				return reach_->fetch_page(url);
			}
		}
		// Wikipedia search API (zero-config fallback for general search)
		if (c.find("wikipedia.org/w/api.php") != std::string::npos && c.find("action=query") != std::string::npos &&
			c.find("list=search") != std::string::npos) {
			static auto const kRe = [] {
				try {
					return boost::regex(R"(srsearch=([^&\"]+))");
				} catch (...) {
					client::log::warn("regex compile failed");
					return boost::regex("");
				}
			}();
			boost::smatch m;
			if (boost::regex_search(c, m, kRe) && m[1].matched) {
				int n = k_default_search_result_count;
				static auto const kLimitRe = [] {
					try {
						return boost::regex(R"(srlimit=(\d+))");
					} catch (...) {
						client::log::warn("regex compile failed");
						return boost::regex("");
					}
				}();
				boost::smatch lm;
				if (boost::regex_search(c, lm, kLimitRe) && lm[1].matched)
					try {
						n = std::stoi(lm[1].str());
					} catch (...) {
					}
				return reach_->search_wikipedia(m[1].str(), n);
			}
		}
		// Other curl (wttr.in, etc.) -- raw exec
		// Block curl to internal/private addresses (SSRF prevention).
		if (!is_safe_curl_target(c))
			return "Error: curl to internal/private addresses is not allowed."
				   " Use dedicated search tools instead.";
		return reach_->exec(c);
	}

	// ── gh ──────────────────────────────────────────────────────────────
	if (client::starts_with(c, "gh ")) {
		static auto const kRe = [] {
			try {
				return boost::regex(R"(gh\s+search\s+repos\s+\"([^\"]*)\"(?:.*?--limit\s+(\d+))?)",
									boost::regex::icase);
			} catch (...) {
				client::log::warn("regex compile failed");
				return boost::regex("");
			}
		}();
		boost::smatch m;
		if (boost::regex_search(c, m, kRe) && m[1].matched) {
			int n = k_default_search_result_count;
			if (m[2].matched)
				try {
					n = std::stoi(m[2].str());
				} catch (...) {
				}
			return reach_->search_github(m[1].str(), n);
		}
		return reach_->exec(c);
	}

	// ── twitter ─────────────────────────────────────────────────────────
	if (client::starts_with(c, "twitter ")) {
		static auto const kRe = [] {
			try {
				return boost::regex(R"(twitter\s+search\s+\"([^\"]*)\"(?:\s+-n\s+(\d+))?)", boost::regex::icase);
			} catch (...) {
				client::log::warn("regex compile failed");
				return boost::regex("");
			}
		}();
		boost::smatch m;
		if (boost::regex_search(c, m, kRe) && m[1].matched) {
			int n = k_default_search_result_count;
			if (m[2].matched)
				try {
					n = std::stoi(m[2].str());
				} catch (...) {
				}
			return reach_->search_twitter(m[1].str(), n);
		}
		return reach_->exec(c);
	}

	// ── mcporter / Exa ──────────────────────────────────────────────────
	if (client::starts_with(c, "mcporter ")) {
		static auto const kRe = [] {
			try {
				return boost::regex(R"(query:\s*\"([^\"]*)\"(?:\s*,\s*numResults:\s*(\d+))?)?)");
			} catch (...) {
				client::log::warn("regex compile failed");
				return boost::regex("");
			}
		}();
		boost::smatch m;
		if (boost::regex_search(c, m, kRe) && m[1].matched) {
			int n = k_default_search_result_count;
			if (m[2].matched)
				try {
					n = std::stoi(m[2].str());
				} catch (...) {
				}
			return reach_->search_exa(m[1].str(), n);
		}
		return reach_->exec(c);
	}

	// ── fallback ────────────────────────────────────────────────────────
	return reach_->exec(c);
}

// ─── tool_loop ───────────────────────────────────────────────────────────

void client::agent_controller::tool_loop(std::vector<chat_message>& messages, std::string const& /*convo_id*/) {
	auto const t_start = std::chrono::steady_clock::now();
	int tool_rounds = 0;

	auto tool_schemas = reach_ ? build_tools() : nlohmann::json::array();

	for (int iter = 0; iter < k_safety_limit; ++iter) {
		auto t_req = std::chrono::steady_clock::now();
		auto resp = llm_->complete_with_tools(messages, tool_schemas);
		record_token_usage(resp.usage);
		auto ms =
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_req).count();

		if (resp.tool_calls.empty()) {
			// ── [SEARCH:] / [FETCH:] legacy tag fallback ──────────────
			auto sq = resp.content.find("[SEARCH:");
			auto fq = resp.content.find("[FETCH:");
			if (sq != std::string::npos || fq != std::string::npos) {
				messages.push_back({"assistant", "", resp.content});
				++tool_rounds;

				if (fq != std::string::npos) {
					// [FETCH:URL] -- use dedicated fetch_page() parser
					auto end = resp.content.find(']', fq + 7);
					auto url = (end != std::string::npos) ? std::string(resp.content.substr(fq + 7, end - fq - 7))
														  : std::string(resp.content.substr(fq + 7));
					log::info("[agent] legacy-tag FETCH: " + url.substr(0, 60));
					auto sr = reach_ ? reach_->fetch_page(client::trim(url)) : std::string{};
					if (sr.empty())
						sr = "(无法获取该网页)";
					messages.push_back({"tool", "", sr, "legacy-fetch"});
				} else {
					// [SEARCH:keyword] -- Exa -> Wikipedia fallback chain
					auto end = resp.content.find(']', sq + 8);
					auto q = (end != std::string::npos) ? std::string(resp.content.substr(sq + 8, end - sq - 8))
														: std::string(resp.content.substr(sq + 8));
					q = client::trim(q);
					log::info("[agent] legacy-tag SEARCH: " + q.substr(0, 60));

					std::string sr;
					// Exa (mcporter) -- best semantic search
					if (reach_ && reach_->capabilities().exa_search) {
						sr = reach_->search_exa(q, 5);
					}
					// Wikipedia API -- zero-config fallback, always works
					if (sr.empty() && reach_) {
						sr = reach_->search_wikipedia(q, 5);
					}
					if (sr.empty())
						sr = "(无搜索结果——请尝试用更具体的关键词，"
							 "或使用 curl 命令直接搜索)";
					messages.push_back({"tool", "", sr, "legacy-search"});
				}
				continue;
			}

			// LLM chose to reply to the user -- no more tools needed.
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

		// Execute tool calls in parallel.
		// Each call is dispatched through route_shell_command() which
		// routes to dedicated parsers (bili_hot, search_github, etc.)
		// for formatted, clipped results — falling back to raw exec()
		// for commands without a dedicated parser.
		// Use weak_ptr to avoid dangling 'this' if controller is destroyed
		// during async execution.

		std::weak_ptr<agent_controller> weak_self = shared_from_this();
		std::vector<std::future<std::pair<std::string, std::string>>> futures;
		futures.reserve(resp.tool_calls.size());

		for (auto const& tc : resp.tool_calls) {
			futures.push_back(std::async(std::launch::async, [weak_self, tc]() -> std::pair<std::string, std::string> {
				auto self = weak_self.lock();
				if (!self)
					return {tc.id, "(controller destroyed)"};

				auto t_exec = std::chrono::steady_clock::now();
				auto args = nlohmann::json::parse(tc.arguments, nullptr, false);
				std::string result;

				if (tc.function_name == "shell") {
					auto cmd = args.value("command", "");
					if (!self->is_safe_command(cmd)) {
						result = "Error: command not allowed.";
					} else {
						client::log::info("[agent] shell: " + cmd.substr(0, 80));
						result = self->route_shell_command(cmd);
						auto exec_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
										   std::chrono::steady_clock::now() - t_exec)
										   .count();
						client::log::info("[agent] shell: " + std::to_string(result.size()) + " chars " +
										  std::to_string(exec_ms) + "ms");
						if (result.empty())
							result = "(no output)";
					}
				} else {
					// P2-2: try plugin tool providers
					result = self->tools_.execute(tc.function_name, args);
				}

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
		log::info("[agent] " + std::to_string(tool_rounds) + " shell calls, " + std::to_string(total_ms) + "ms total");
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
			messages.insert(messages.begin(),
							{"system", "", std::string("当前时间: ") + buf});
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
