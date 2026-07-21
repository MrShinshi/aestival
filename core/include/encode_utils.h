/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <string>
#include <string_view>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <boost/locale/encoding_utf.hpp>

namespace client {

inline std::string sanitize_utf8(std::string const& s) {
	if (s.empty())
		return s;
	try {
		boost::locale::conv::utf_to_utf<char>(s.data(), s.data() + s.size(), boost::locale::conv::stop);
		return s;
	} catch (boost::locale::conv::conversion_error const&) {
		return boost::locale::conv::utf_to_utf<char>(s.data(), s.data() + s.size(), boost::locale::conv::skip);
	}
}

inline std::string url_encode(std::string_view src) {
	std::string dst;
	dst.reserve(src.size() * 3);
	for (unsigned char c : src) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
			dst += static_cast<char>(c);
		else if (c == ' ')
			dst += '+';
		else {
			char hex[4];
			std::snprintf(hex, sizeof(hex), "%%%02X", c);
			dst += hex;
		}
	}
	return dst;
}

inline std::string trim(std::string_view s) {
	auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
	std::string r(s);
	r.erase(r.begin(), std::find_if(r.begin(), r.end(), not_space));
	r.erase(std::find_if(r.rbegin(), r.rend(), not_space).base(), r.end());
	return r;
}

inline bool starts_with(std::string_view s, std::string_view prefix) {
	return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

inline std::string to_ascii_lower(std::string_view s) {
	std::string r(s);
	std::transform(r.begin(), r.end(), r.begin(), [](unsigned char ch) -> char {
		return ch < 128 ? static_cast<char>(std::tolower(ch)) : static_cast<char>(ch);
	});
	return r;
}

} // namespace client
