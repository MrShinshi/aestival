/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "encode_utils.h"
#include "plugin.h"

#include <memory>
#include <string>
#include <string_view>

namespace client::plugins {

// ─── agent_reach_plugin ──────────────────────────────────────────────────
//
// Minimal plugin: handles the help command only.
//
// All search/fetch tools have moved to MCP servers (mcporter, etc.).
// This plugin now only intercepts "help" / "帮助" to show available
// commands and backends.

struct agent_reach_plugin final : public plugin {
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
		return c == "帮助" || c == "help" || c == "/help";
	}

	plugin_result handle(plugin_context& ctx) override {
		auto raw = std::string(ctx.content());
		auto c = client::trim(raw);

		if (c == "帮助" || c == "help" || c == "/help") {
			ctx.reply(build_help());
			return {true, true};
		}

		return {false, false};
	}

	private:
	static std::string build_help() {
		std::string h;
		h = "## 帮助\n\n";
		h += "直接聊天即可 — 我会自动搜索网页、查找信息。\n\n";
		h += "### 系统命令\n\n";
		h += "`switch mode` — 切换 agent/plugin 模式\n";
		h += "`clear` — 清除当前会话上下文\n";
		h += "`usage` — 查看 token 用量\n";
		h += "`stop` — 停止当前回复\n";
		h += "`self-iterate` — 触发自我迭代\n";
		h += "`help` / `帮助` — 显示此帮助\n";
		return h;
	}
};

} // namespace client::plugins
