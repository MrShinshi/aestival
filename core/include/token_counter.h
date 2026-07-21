/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "chat_context_manager.h"

namespace client {

// ─── token_counter ────────────────────────────────────────────────────────
// Simple heuristics to estimate LLM token count from raw UTF-8 content.
// Not exact (no real tokenizer) but close enough for context-window
// gating decisions.  Errors on the conservative side (over-estimates
// rather than under-estimates).

struct token_counter {
	// Improved estimation: distinguish CJK from ASCII for better accuracy.
	// CJK (3 bytes/char, ~1.5 tokens/char) → ~0.5 tokens/byte
	// ASCII (1 byte/char, ~0.25 tokens/char) → ~0.25 tokens/byte
	static constexpr double k_cjk_bytes_per_token = 2.0;   // 3/1.5
	static constexpr double k_ascii_bytes_per_token = 4.0; // 1/0.25

	static int64_t estimate_tokens(std::string_view s) {
		int64_t cjk_bytes = 0;
		int64_t ascii_bytes = 0;
		for (size_t i = 0; i < s.size();) {
			unsigned char c = static_cast<unsigned char>(s[i]);
			if (c >= 0xE0) {
				cjk_bytes += 3;
				i += 3;
			} // 3-byte UTF-8 (CJK)
			else if (c >= 0xC0) {
				cjk_bytes += 2;
				i += 2;
			} // 2-byte UTF-8
			else if (c >= 0x80) {
				cjk_bytes += 2;
				i += 2;
			} // continuation (shouldn't happen)
			else {
				ascii_bytes += 1;
				i += 1;
			}
		}
		return static_cast<int64_t>(cjk_bytes / k_cjk_bytes_per_token + ascii_bytes / k_ascii_bytes_per_token);
	}

	// Estimate total tokens across a message list (system + user + assistant).
	static int64_t estimate_tokens(std::vector<chat_message> const& msgs) {
		int64_t total = 0;
		for (auto const& m : msgs)
			total += estimate_tokens(m.content);
		return total;
	}

	// Context window sizes for common models.
	// Used for logging / ratio display, not enforcement.
	static constexpr int64_t default_context_window = 131072; // 128K
	static constexpr int64_t openai_gpt4o_window = 131072;	  // 128K

	// Log-friendly ratio string: "12345 / 131072 (9.4%)"
	static std::string ratio_str(int64_t tokens, int64_t window) {
		int pct = window > 0 ? static_cast<int>(tokens * 100 / window) : 0;
		return std::to_string(tokens) + " / " + std::to_string(window) + " (" + std::to_string(pct) + "%)";
	}
};

} // namespace client
