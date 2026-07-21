/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "agent_reach_client.h"
#include "plugin.h"

#include <memory>
#include <string>
#include <string_view>

namespace client::plugins {

// ─── agent_reach_plugin ──────────────────────────────────────────────────
//
// Minimal plugin: only handles explicit user commands.  All natural-
// language intent routing is done by the LLM through [SEARCH:] / [FETCH:]
// tags in agent mode.
//
// Explicit commands intercepted:
//   help / 帮助           → show available backends
//   fetch <URL> / 读网页   → Jina Reader direct fetch
//   /bilibili <X>          → B站 search
//   /v2ex <X>              → V2EX search
//   /github <X>            → GitHub code search
//   /twitter <X>           → Twitter search
//   搜索 <X> / search <X>   → explicit search via [SEARCH:] equivalent
//
// Everything else → passes through to LLM (agent mode) or is ignored
// (plugin mode, for other plugins to handle).

struct agent_reach_plugin final : public plugin {
	static constexpr int k_default_search_count = 5;

	explicit agent_reach_plugin(std::shared_ptr<agent_reach_client> client) : client_(std::move(client)) {
	}

	std::string_view name() const override {
		return "agent_reach";
	}
	int priority() const override {
		return 100;
	}
	plugin_capability capabilities() const override {
		return plugin_capability::send_message;
	}

	bool can_handle(message_event const& message) const override {
		auto c = client::trim(message.content);
		if (c.empty())
			return false;
		if (c == "hello" || c == "你好" || c == "ping")
			return false;
		if (c == "switch mode" || c == "clear" || c == "usage" || c == "stop")
			return false;

		return client::starts_with(c, "/bilibili ") || client::starts_with(c, "/bili ") ||
			   client::starts_with(c, "/v2ex ") || client::starts_with(c, "/github ") ||
			   client::starts_with(c, "/gh ") || client::starts_with(c, "/twitter ") || client::starts_with(c, "/x ") ||
			   client::starts_with(c, "fetch ") || client::starts_with(c, "读网页 ") || c == "帮助" || c == "help" ||
			   c == "/help" || c == "搜索帮助" || c == "search help" || c == "/search_help";
	}

	plugin_result handle(plugin_context& ctx) override {
		auto raw = std::string(ctx.content());
		auto c = client::trim(raw);

		// ── help ──────────────────────────────────────────────────────
		if (c == "帮助" || c == "help" || c == "/help" || c == "搜索帮助" || c == "search help" ||
			c == "/search_help") {
			ctx.reply(build_help());
			return {true, true};
		}

		// ── page fetch ────────────────────────────────────────────────
		if (client::starts_with(c, "fetch ") || client::starts_with(c, "读网页 ")) {
			auto url = client::trim(extract_after(c, client::starts_with(c, "fetch ") ? 6 : 7));
			if (url.empty()) {
				ctx.reply("用法: fetch <URL>");
				return {true, true};
			}
			// SSRF guard: reject internal URLs
			static const char* kBlocked[] = {"//localhost", "//127.",	  "//10.",	 "//192.168.",
											 "//172.",		"//169.254.", "//[::1]", "//0.0.0.0"};
			for (auto* pat : kBlocked) {
				if (url.find(pat) != std::string::npos) {
					ctx.reply("不允许访问内网地址");
					return {true, true};
				}
			}
			ctx.reply("📖 正在读取网页...");
			auto page = client_->fetch_page(url);
			ctx.reply(page.empty() ? "读取失败" : page);
			return {true, true};
		}

		// ── extract query ─────────────────────────────────────────────
		std::string query;
		std::string action; // "bilibili" | "v2ex" | "github" | "twitter" | ""

		if (client::starts_with(c, "/bilibili ") || client::starts_with(c, "/bili ")) {
			query = extract_after(c, c.find(' ') + 1);
			action = "bilibili";
		} else if (client::starts_with(c, "/v2ex ")) {
			query = extract_after(c, c.find(' ') + 1);
			action = "v2ex";
		} else if (client::starts_with(c, "/github ") || client::starts_with(c, "/gh ")) {
			query = extract_after(c, c.find(' ') + 1);
			action = "github";
		} else if (client::starts_with(c, "/twitter ") || client::starts_with(c, "/x ")) {
			query = extract_after(c, c.find(' ') + 1);
			action = "twitter";
		}

		query = client::trim(query);
		if (query.empty()) {
			ctx.reply("请输入搜索关键词。例如: 搜索 上海天气");
			return {true, true};
		}

		// ── dispatch ──────────────────────────────────────────────────
		std::string result;
		if (action == "bilibili") {
			ctx.reply("📺 正在搜索 B站: " + query + " ...");
			result = client_->search_bilibili(query, k_default_search_count);
		} else if (action == "v2ex") {
			ctx.reply("💬 正在搜索 V2EX: " + query + " ...");
			result = client_->search_v2ex(query);
		} else if (action == "github") {
			ctx.reply("💻 正在搜索 GitHub: " + query + " ...");
			result = client_->search_github(query, k_default_search_count);
		} else if (action == "twitter") {
			ctx.reply("🐦 正在搜索 Twitter: " + query + " ...");
			result = client_->search_twitter(query, k_default_search_count);
		} else {
			ctx.reply("未知命令");
			return {true, true};
		}

		if (result.empty())
			ctx.reply("未找到结果。请换个关键词试试~");
		else
			ctx.reply(result);

		return {true, true};
	}

	private:
	std::shared_ptr<agent_reach_client> client_;

	static std::string extract_after(std::string_view s, size_t pos) {
		return pos >= s.size() ? std::string{} : std::string(s.substr(pos));
	}

	std::string build_help() const {
		auto const& cap = client_->capabilities();
		std::string ok = "✓", no = "✗";
		std::string h;
		h = "## 搜索命令\n\n";
		h += "| 后端 | 状态 | 说明 |\n|------|------|------|\n";
		h += "| Exa 语义搜索 | " + std::string(cap.exa_search ? ok : no) + " | 全网搜索 |\n";
		h += "| Jina Reader  | " + ok + " | 网页全文阅读 |\n";
		h += "| V2EX         | " + ok + " | 中文技术社区 |\n";
		h += "| B站          | " + std::string(cap.bilibili ? ok : no) + " | bili-cli |\n";
		h += "| GitHub       | " + std::string(cap.github ? ok : no) + " | gh CLI |\n";
		h += "| Twitter      | " + std::string(cap.twitter ? ok : no) + " | twitter-cli |\n\n";
		h += "### 命令\n\n";
		h += "直接聊天即可，无需命令 — 我会自动搜索。\n\n";
		h += "快捷命令（跳过AI直接搜）：\n\n";
		h += "`/bilibili <词>` — B站搜索\n";
		h += "`/v2ex <词>` — V2EX 搜索\n";
		h += "`/github <词>` — GitHub 搜索\n";
		h += "`fetch <URL>` — 网页全文\n";
		h += "`/help` — 此帮助\n";
		return h;
	}
};

} // namespace client::plugins
