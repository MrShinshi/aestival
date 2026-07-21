/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "platform/deepseek.h"
#include <string>
#include <string_view>

namespace platform::openai {

using raw_chat_response = platform::deepseek::raw_chat_response;

raw_chat_response chat(nlohmann::json const& messages, nlohmann::json const* tools, std::string_view api_key,
					   std::string_view model, std::string_view base_url, bool verify_tls);
std::string make_time_string();

} // namespace platform::openai
