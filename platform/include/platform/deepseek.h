/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>

namespace platform::deepseek {

struct raw_chat_response {
	std::string content;
	nlohmann::json tool_calls;
	nlohmann::json usage;
};

raw_chat_response chat(nlohmann::json const& messages, nlohmann::json const* tools, std::string_view api_key,
					   std::string_view model, bool verify_tls);

std::string query_balance(std::string_view api_key, bool verify_tls);
std::string make_time_string();

// ─── DeepSeek Platform usage API (web session auth, not API key) ───────
// See docs: https://platform.deepseek.com/usage (reverse-engineered)

nlohmann::json query_usage_amount(std::string_view user_token, std::string_view waf_cookie, int64_t start_sec,
								  int64_t end_sec, bool verify_tls);

nlohmann::json query_usage_cost(std::string_view user_token, std::string_view waf_cookie, int64_t start_sec,
								int64_t end_sec, bool verify_tls);

} // namespace platform::deepseek
