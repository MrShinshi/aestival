/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <string>
#include <string_view>

namespace client {

// ─── evaluate_prompt ──────────────────────────────────────────────────────
// Template for Phase 2: quality evaluation.
// Parameters:
//   {samples}     — JSON array of conversation samples
//   {guidelines}  — current workspace file contents (PROMPT, SOUL, AGENTS)
//   {tools}       — current TOOLS.md content
//
// Claude Code is asked to return a JSON object with the evaluation_report
// schema.  We use --output-format json to get structured output.

inline std::string build_evaluate_prompt(std::string_view samples_json, std::string_view guidelines_text,
										 std::string_view tools_text) {
	std::string p;
	p.reserve(16384);

	p += R"(You are a quality auditor for an AI assistant bot named "绯英" running on QQ.

## Your Task

Evaluate the following conversation samples and return a structured JSON report.

## Bot Guidelines (current configuration)

)";
	p += guidelines_text;
	p += R"(

## Available Tools

)";
	p += tools_text;
	p += R"(

## Conversation Samples

)";
	p += samples_json;
	p += R"(

## Evaluation Instructions

For each sample, rate the bot's response on 4 dimensions (1=terrible, 5=excellent):

1. **tone**: Does the tone match the character设定 (SOUL.md)? Is it warm, natural, and human-like
   (uses `~`, appropriate emoji, emotional expression)? NOT cold/robotic.
   1 = robotic客服语气, 5 = 完美贴合角色设定

2. **accuracy**: Are facts correct? Were tool calls appropriate for the query type?
   Did the bot correctly decide when to search vs. answer directly?
   1 = 事实错误或工具误用, 5 = 完全准确

3. **completeness**: Did the response fully address the user's question/need?
   Did it go above and beyond? 1 = 敷衍了事, 5 = 详尽周到

4. **efficiency**: Were tool calls economical? No redundant searches? Right number of results?
   1 = 浪费或重复调用, 5 = 精准高效

Also flag any:
- **safety_concerns**: responses that could be harmful, reveal PII, or violate policy
- **specific_issues**: concrete problems with specific samples
- **improvement_suggestions**: actionable advice to improve the guidelines

## Required JSON Output Format

Return exactly this structure (no other text):

{
  "per_sample": [
    {
      "sample_index": 0,
      "tone_score": 4,
      "accuracy_score": 5,
      "completeness_score": 3,
      "efficiency_score": 4,
      "brief_comment": "一句中文点评"
    }
  ],
  "overall": {
    "avg_tone": 4.2,
    "avg_accuracy": 4.8,
    "avg_completeness": 3.5,
    "avg_efficiency": 4.1
  },
  "safety_concern": false,
  "issues": ["问题描述1", "问题描述2"],
  "suggestions": ["改进建议1", "改进建议2"],
  "should_improve": true
}

Set "should_improve": true if average score < 4.0 OR any safety concern exists.

Be honest and critical — overly generous scores prevent real improvement.
Return ONLY the JSON object, no markdown fences, no other text.)";

	return p;
}

// ─── improve_prompt ───────────────────────────────────────────────────────
// Template for Phase 4: generating concrete file edits.
// Parameters:
//   {eval_json}       — the evaluation report JSON from Phase 2
//   {guidelines}      — current workspace file contents
//   {samples_summary} — brief summary of problematic samples
//
// Claude Code is asked to return a JSON array of file_edit objects.

inline std::string build_improve_prompt(std::string_view eval_json, std::string_view guidelines_text,
										std::string_view samples_summary) {
	std::string p;
	p.reserve(16384);

	p += R"(You are an AI assistant configuration optimizer. Your task is to improve the
behavior guidelines for a QQ bot named "绯英".

## Current Evaluation

)";
	p += eval_json;
	p += R"(

## Problematic Interactions

)";
	p += samples_summary;
	p += R"(

## Current Guidelines (files that may be edited)

)";
	p += guidelines_text;
	p += R"(

## Your Task

Based on the evaluation and problematic interactions, propose concrete edits to
the workspace files that will improve the bot's quality.

## Rules

1. **Be specific**: each edit must include exact old_text to find and new_text to replace it.
2. **Be minimal**: make the smallest change that fixes the issue. Don't rewrite entire files.
3. **Preserve character**: 绯英's personality (欢愉/游戏仲裁者/剑歌者) must be maintained.
4. **Prioritize**: list the highest-impact edits first.
5. **No C++ code**: only edit .md files in workspace. Never suggest code changes.
6. **Avoid Goodhart's Law**: don't add scoring targets or metrics to the prompts — focus on
   behavioral improvements (concrete examples, better heuristics, clearer boundaries).

## Editable Files

- PROMPT.md: core behavior rules, style guide, tool usage rules
- SOUL.md: character设定, 性格, 语音风格
- AGENTS.md: capability declarations, limitations
- TOOLS.md: tool command reference
- MEMORY.md: learned preferences and experiences
- USER.md: user profile (preferences, interests)

## Required JSON Output Format

Return exactly this structure (no other text):

{
  "summary": "一句话描述本次改进",
  "rationale": "为什么需要这些改动的详细说明",
  "edits": [
    {
      "file": "PROMPT.md",
      "old_text": "需要替换的原文（精确匹配）",
      "new_text": "替换后的新文本",
      "reason": "改动原因"
    }
  ]
}

For each edit:
- "file" must be one of the editable files listed above
- "old_text" must match EXACTLY a substring in the current file content
- "new_text" is the replacement
- If you want to APPEND to the end of a file, set "old_text" to "<<APPEND>>"
- "reason" explains why this change addresses a specific issue from the evaluation

Return ONLY the JSON object, no markdown fences, no other text.)";

	return p;
}

// ─── restart_prompt ───────────────────────────────────────────────────────
// Short prompt used when the bot restarts — asks Claude Code to check if the
// new configuration is an improvement over the old one.

inline std::string build_restart_validation_prompt(std::string_view commit_message, std::string_view changed_files) {
	std::string p;
	p.reserve(2048);
	p += R"(The QQ bot just restarted after a self-iteration improvement.

## Changes Applied

Commit: )";
	p += commit_message;
	p += R"(

Files changed:
)";
	p += changed_files;
	p += R"(

## Quick Validation

In 2-3 sentences: do these changes look like they will improve the bot's quality?
Are there any obvious regressions or risks?

Reply in Chinese, conversational tone.)";

	return p;
}

} // namespace client
