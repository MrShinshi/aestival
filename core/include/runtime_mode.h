/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once
#include <magic_enum/magic_enum.hpp>

namespace client {

enum struct runtime_mode { plugin, agent };

inline std::string_view to_string(runtime_mode value) {
	auto name = magic_enum::enum_name(value);
	return name.empty() ? "plugin" : name;
}

} // namespace client
