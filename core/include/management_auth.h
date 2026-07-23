/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <openssl/hmac.h>
#include <nlohmann/json.hpp>

#include <ctime>
#include <stdexcept>
#include <string>
#include <string_view>

namespace client::mgmt {

// ─── Minimal JWT verifier ──────────────────────────────────────────────────
//
// Only supports HS256 (HMAC-SHA256).  Designed for internal use between the
// Web UI backend (issuer) and the bot management API (verifier).
//
// Token format: header.payload.signature
//   - header:   {"alg":"HS256","typ":"JWT"}
//   - payload:  {"sub":"<github-user>","iat":...,"exp":...}
//   - signature: HMAC-SHA256(header.payload, secret)

struct jwt_verifier {
	explicit jwt_verifier(std::string secret) : secret_(std::move(secret)) {}

	// Verify a Bearer token.  Returns the "sub" claim (GitHub username)
	// on success.  Throws std::runtime_error on any failure.
	std::string verify(std::string_view token) const {
		if (secret_.empty())
			throw std::runtime_error("JWT secret not configured");

		// Split into three parts
		auto dot1 = token.find('.');
		auto dot2 = token.find('.', dot1 + 1);
		if (dot1 == std::string_view::npos || dot2 == std::string_view::npos)
			throw std::runtime_error("invalid JWT format");

		auto header_b64 = token.substr(0, dot1);
		auto payload_b64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
		auto sig_b64 = token.substr(dot2 + 1);

		// Verify signature
		auto expected_sig = sign(header_b64, payload_b64);
		if (!constant_time_eq(sig_b64, expected_sig))
			throw std::runtime_error("JWT signature mismatch");

		// Decode payload
		auto payload_json = b64url_decode(payload_b64);
		auto payload = nlohmann::json::parse(payload_json, nullptr, false);
		if (payload.is_discarded())
			throw std::runtime_error("invalid JWT payload JSON");

		// Check expiration
		auto exp = payload.value("exp", 0);
		auto now = static_cast<std::time_t>(std::time(nullptr));
		if (exp > 0 && now > exp)
			throw std::runtime_error("JWT expired");

		return payload.value("sub", "unknown");
	}

	private:
	std::string secret_;

	// ── Base64URL decode ───────────────────────────────────────────────
	static std::string b64url_decode(std::string_view input) {
		// Convert Base64URL → Base64
		std::string b64;
		b64.reserve(input.size() + 4);
		for (char c : input) {
			if (c == '-')
				b64 += '+';
			else if (c == '_')
				b64 += '/';
			else
				b64 += c;
		}
		// Add padding
		while (b64.size() % 4)
			b64 += '=';

		// Decode using OpenSSL's EVP
		return b64_decode(b64);
	}

	static std::string b64_decode(std::string_view input) {
		// Estimate output length
		int out_len = static_cast<int>(input.size()) * 3 / 4;
		std::string out(out_len, '\0');

		// Use OpenSSL's internal base64 decoder via BIO
		// Simplified: use EVP_DecodeBlock
		int actual = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(out.data()),
									  reinterpret_cast<unsigned char const*>(input.data()),
									  static_cast<int>(input.size()));
		if (actual < 0)
			throw std::runtime_error("base64 decode failed");
		out.resize(actual);
		return out;
	}

	// ── HMAC-SHA256 ────────────────────────────────────────────────────
	std::string sign(std::string_view header_b64, std::string_view payload_b64) const {
		std::string message;
		message.reserve(header_b64.size() + 1 + payload_b64.size());
		message += header_b64;
		message += '.';
		message += payload_b64;

		unsigned char result[EVP_MAX_MD_SIZE];
		unsigned int len = 0;
		HMAC(EVP_sha256(), secret_.data(), static_cast<int>(secret_.size()),
			 reinterpret_cast<unsigned char const*>(message.data()), message.size(), result, &len);

		return b64url_encode(std::string_view(reinterpret_cast<char const*>(result), len));
	}

	static std::string b64url_encode(std::string_view data) {
		// Use OpenSSL's EVP_EncodeBlock, then convert to base64url
		int out_len = ((static_cast<int>(data.size()) + 2) / 3) * 4;
		std::string out(out_len, '\0');
		int actual = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),
									  reinterpret_cast<unsigned char const*>(data.data()),
									  static_cast<int>(data.size()));
		out.resize(actual);
		// Strip padding and convert to base64url
		while (!out.empty() && out.back() == '=')
			out.pop_back();
		for (auto& c : out) {
			if (c == '+')
				c = '-';
			else if (c == '/')
				c = '_';
		}
		return out;
	}

	// Constant-time comparison to prevent timing attacks.
	static bool constant_time_eq(std::string_view a, std::string_view b) {
		if (a.size() != b.size())
			return false;
		unsigned char diff = 0;
		for (size_t i = 0; i < a.size(); ++i)
			diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
		return diff == 0;
	}
};

} // namespace client::mgmt
