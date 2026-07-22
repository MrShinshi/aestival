/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <boost/locale/encoding_utf.hpp>
#include <string>
#include <string_view>

namespace platform::detail {

inline std::string sanitize_utf8(std::string_view s) {
	if (s.empty())
		return {};
	return boost::locale::conv::utf_to_utf<char>(s.data(), s.data() + s.size(), boost::locale::conv::skip);
}

} // namespace platform::detail
