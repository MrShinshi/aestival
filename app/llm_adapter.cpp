/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "model_client.h"
#include "platform/deepseek.h"
#include "platform/openai.h"
#include "bot_config.h"

#include <nlohmann/json.hpp>

static nlohmann::json build_messages_json(std::vector<client::chat_message> const& msgs) {
	auto arr = nlohmann::json::array();
	for (auto const& m : msgs) {
		auto j = nlohmann::json::object({{"role", m.role}, {"content", m.content}});
		if (m.role == "tool")
			j["tool_call_id"] = m.tool_call_id;
		if (m.role == "assistant" && !m.tool_calls_json.empty()) {
			try {
				j["tool_calls"] = nlohmann::json::parse(m.tool_calls_json);
				j["content"] = nullptr;
			} catch (...) {
			}
		}
		arr.push_back(std::move(j));
	}
	return arr;
}

struct deepseek_adapter : client::model_client {
	std::string key_, model_, user_token_, waf_cookie_;
	bool verify_tls_;
	deepseek_adapter(std::string k, std::string m, std::string ut, std::string wc, bool v)
		: key_(k), model_(m), user_token_(ut), waf_cookie_(wc), verify_tls_(v) {
	}
	bool is_enabled() const override {
		return !key_.empty();
	}
	std::string model_name() const override {
		return model_;
	}
	std::string provider_name() const override {
		return "deepseek";
	}

	std::string complete(std::string_view prompt) const override {
		auto msgs = nlohmann::json::array(
			{{{"role", "system"},
			  {"content", "You are a concise bot assistant. Current time: " + platform::deepseek::make_time_string()}},
			 {{"role", "user"}, {"content", std::string(prompt)}}});
		return platform::deepseek::chat(msgs, nullptr, key_, model_, verify_tls_).content;
	}
	std::string complete_messages(std::vector<client::chat_message> const& msgs) const override {
		auto j = build_messages_json(msgs);
		auto tm = nlohmann::json::object(
			{{"role", "system"}, {"content", "Current time: " + platform::deepseek::make_time_string()}});
		size_t ins = 0;
		while (ins < j.size() && j[ins]["role"] == "system")
			++ins;
		j.insert(j.begin() + static_cast<long>(ins), std::move(tm));
		return platform::deepseek::chat(j, nullptr, key_, model_, verify_tls_).content;
	}
	client::model_response complete_with_tools(std::vector<client::chat_message> const& msgs,
											   nlohmann::json const& tools) const override {
		auto j = build_messages_json(msgs);
		auto tm = nlohmann::json::object(
			{{"role", "system"}, {"content", "Current time: " + platform::deepseek::make_time_string()}});
		size_t ins = 0;
		while (ins < j.size() && j[ins]["role"] == "system")
			++ins;
		j.insert(j.begin() + static_cast<long>(ins), std::move(tm));
		auto raw = platform::deepseek::chat(j, &tools, key_, model_, verify_tls_);
		client::model_response r;
		r.content = raw.content;
		if (!raw.tool_calls.is_null() && raw.tool_calls.is_array())
			for (auto const& tc : raw.tool_calls) {
				auto const& fn = tc["function"];
				r.tool_calls.push_back({tc.value("id", ""), fn.value("name", ""), fn.value("arguments", "")});
			}
		r.usage = raw.usage;
		return r;
	}
	std::string query_balance() const override {
		return platform::deepseek::query_balance(key_, verify_tls_);
	}

	std::string query_usage_json(int year, int month) const override {
		if (user_token_.empty() || waf_cookie_.empty())
			return R"({"error":"未配置 DeepSeek Platform 登录态（user_token / waf_cookie），用量查询不可用。请从 platform.deepseek.com 抓取浏览器 session 填入 config/bot_config.json。"})";
		try {
			auto amount =
				platform::deepseek::query_usage_amount(user_token_, waf_cookie_, year, month, verify_tls_);
			auto cost = platform::deepseek::query_usage_cost(user_token_, waf_cookie_, year, month, verify_tls_);
			auto j = nlohmann::json::object();
			j["amount"] = std::move(amount);
			j["cost"] = std::move(cost);
			return j.dump();
		} catch (std::exception const& ex) {
			return std::string("{\"error\":\"") + ex.what() + "\"}";
		}
	}
};

struct openai_adapter : client::model_client {
	std::string key_, model_, base_url_;
	bool verify_tls_;
	openai_adapter(std::string k, std::string m, std::string b, bool v)
		: key_(k), model_(m), base_url_(b), verify_tls_(v) {
	}
	bool is_enabled() const override {
		return !key_.empty();
	}
	std::string model_name() const override {
		return model_;
	}
	std::string provider_name() const override {
		return "openai";
	}

	std::string complete(std::string_view prompt) const override {
		auto msgs = nlohmann::json::array(
			{{{"role", "system"},
			  {"content", "You are a concise bot assistant. Current time: " + platform::openai::make_time_string()}},
			 {{"role", "user"}, {"content", std::string(prompt)}}});
		return platform::openai::chat(msgs, nullptr, key_, model_, base_url_, verify_tls_).content;
	}
	std::string complete_messages(std::vector<client::chat_message> const& msgs) const override {
		auto j = build_messages_json(msgs);
		auto tm = nlohmann::json::object(
			{{"role", "system"}, {"content", "Current time: " + platform::openai::make_time_string()}});
		size_t ins = 0;
		while (ins < j.size() && j[ins]["role"] == "system")
			++ins;
		j.insert(j.begin() + static_cast<long>(ins), std::move(tm));
		return platform::openai::chat(j, nullptr, key_, model_, base_url_, verify_tls_).content;
	}
	client::model_response complete_with_tools(std::vector<client::chat_message> const& msgs,
											   nlohmann::json const& tools) const override {
		auto j = build_messages_json(msgs);
		auto tm = nlohmann::json::object(
			{{"role", "system"}, {"content", "Current time: " + platform::openai::make_time_string()}});
		size_t ins = 0;
		while (ins < j.size() && j[ins]["role"] == "system")
			++ins;
		j.insert(j.begin() + static_cast<long>(ins), std::move(tm));
		auto raw = platform::openai::chat(j, &tools, key_, model_, base_url_, verify_tls_);
		client::model_response r;
		r.content = raw.content;
		if (!raw.tool_calls.is_null() && raw.tool_calls.is_array())
			for (auto const& tc : raw.tool_calls) {
				auto const& fn = tc["function"];
				r.tool_calls.push_back({tc.value("id", ""), fn.value("name", ""), fn.value("arguments", "")});
			}
		r.usage = raw.usage;
		return r;
	}
};

std::unique_ptr<client::model_client> make_model_client(client::bot_config const& cfg) {
	// Respect configured llm_provider; fall back to deepseek when unset.
	if (cfg.llm_provider == "openai")
		return std::make_unique<openai_adapter>(cfg.openai_api_key, cfg.openai_model, cfg.openai_base_url,
												cfg.verify_tls);
	return std::make_unique<deepseek_adapter>(cfg.deepseek_api_key, cfg.deepseek_model, cfg.deepseek_user_token,
											  cfg.deepseek_waf_cookie, cfg.verify_tls);
}
