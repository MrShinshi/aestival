/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "self_iteration.h"
#include "self_iteration_prompts.h"
#include "agent_reach_client.h"
#include "chat_storage_sqlite.h"
#include "encode_utils.h"
#include "log.h"

#include <sstream>
#include <fstream>
#include <regex>

namespace client {

namespace {

// ─── constants ────────────────────────────────────────────────────────────

static constexpr int kDefaultSampleCount = 10;
static constexpr double kImproveThreshold = 4.0; // avg score below this → improve
static constexpr int kMinIntervalMinutes = 60;	 // min time between auto iterations
static constexpr int kClaudeTimeoutSeconds = 120;
static constexpr int k_default_query_limit = 50;
static constexpr int k_min_samples = 3;

// ─── file whitelist ──────────────────────────────────────────────────────
// Only these files (relative to workspace) may be edited.

static bool is_editable_file(std::string_view filename) {
	static const char* kAllowed[] = {"PROMPT.md", "SOUL.md", "AGENTS.md",  "TOOLS.md",
									 "MEMORY.md", "USER.md", "IDENTITY.md"};
	for (auto* a : kAllowed)
		if (filename == a)
			return true;
	return false;
}

// ─── JSON sanitization ───────────────────────────────────────────────────
// Strip markdown code fences that Claude Code sometimes wraps around JSON.

static std::string strip_json_fences(std::string_view s) {
	std::string r(s);
	// Remove leading ```json or ``` fences
	static auto const kLeading = [] {
		try {
			return boost::regex(R"(^```(?:json)?\s*\n?)");
		} catch (...) {
			return boost::regex("");
		}
	}();
	try {
		r = boost::regex_replace(r, kLeading, "", boost::format_first_only);
	} catch (...) {
	}
	// Remove trailing ``` fences
	static auto const kTrailing = [] {
		try {
			return boost::regex(R"(\n?```\s*$)");
		} catch (...) {
			return boost::regex("");
		}
	}();
	try {
		r = boost::regex_replace(r, kTrailing, "", boost::format_first_only);
	} catch (...) {
	}
	return r;
}

// ─── timestamp ────────────────────────────────────────────────────────────

static std::string now_iso() {
	auto t = std::chrono::system_clock::now();
	auto tt = std::chrono::system_clock::to_time_t(t);
	std::tm tm{};
#ifdef _WIN32
	localtime_s(&tm, &tt);
#else
	localtime_r(&tt, &tm);
#endif
	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
	return std::string(buf);
}

// ─── helper: read recent user+assistant pairs from SQLite ─────────────────
// We query across all conversation IDs for the most recent exchanges.

struct sample_row {
	std::string convo_id;
	std::string created_at;
	std::string role;
	std::string nick;
	std::string content;
};

static std::vector<sample_row> query_recent_messages(sqlite3* db, int limit = k_default_query_limit) {
	std::vector<sample_row> rows;
	if (!db)
		return rows;

	// Get recent user messages with their timestamps, then join with the
	// next assistant message (the bot's reply).
	sqlite3_stmt* stmt = nullptr;
	char const* sql = "SELECT m1.convo_id, m1.created_at, m1.role, m1.nick, m1.content "
					  "FROM messages m1 "
					  "WHERE m1.role IN ('user','assistant') "
					  "ORDER BY m1.created_at DESC, m1.id DESC "
					  "LIMIT ?1;";

	if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
		log::error(std::string("[self-iterate] query failed: ") + sqlite3_errmsg(db));
		return rows;
	}

	sqlite3_bind_int(stmt, 1, limit);

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		sample_row r;
		auto* cid_text = reinterpret_cast<char const*>(sqlite3_column_text(stmt, 0));
		r.convo_id = cid_text ? cid_text : "";
		r.created_at = std::to_string(sqlite3_column_int64(stmt, 1));
		auto* role_text = reinterpret_cast<char const*>(sqlite3_column_text(stmt, 2));
		r.role = role_text ? role_text : "";
		auto* nick_text = reinterpret_cast<char const*>(sqlite3_column_text(stmt, 3));
		r.nick = nick_text ? nick_text : "";
		r.content = reinterpret_cast<char const*>(sqlite3_column_text(stmt, 4))
						? reinterpret_cast<char const*>(sqlite3_column_text(stmt, 4))
						: "";
		rows.push_back(std::move(r));
	}
	sqlite3_finalize(stmt);
	return rows;
}

// ─── helper: pair user messages with the next assistant message ──────────
// We scan chronologically (forward) and pair each user message with the
// assistant message that immediately follows it in the same conversation.

static std::vector<conversation_sample> pair_exchanges(std::vector<sample_row> const& rows) {
	// First, sort chronologically (oldest first).
	auto sorted = rows;
	std::reverse(sorted.begin(), sorted.end());

	std::vector<conversation_sample> samples;
	std::string pending_convo;
	std::string pending_ts;
	std::string pending_user;

	for (auto const& r : sorted) {
		if (r.role == "user") {
			pending_convo = r.convo_id;
			pending_ts = r.created_at;
			// Include nick for group context
			pending_user = r.nick.empty() ? r.content : "[" + r.nick + "]: " + r.content;
		} else if (r.role == "assistant" && r.convo_id == pending_convo && !pending_user.empty()) {
			conversation_sample s;
			s.convo_id = r.convo_id;
			s.timestamp = pending_ts;
			s.user_message = pending_user;
			s.bot_response = r.content;
			// Count legacy tags as tool calls for metrics
			s.tool_calls = 0;
			if (s.bot_response.find("[SEARCH:") != std::string::npos)
				++s.tool_calls;
			if (s.bot_response.find("[FETCH:") != std::string::npos)
				++s.tool_calls;
			samples.push_back(std::move(s));
			pending_user.clear();
		}
	}
	return samples;
}

} // namespace

// ─── constructor ──────────────────────────────────────────────────────────

self_iteration_engine::self_iteration_engine(self_iteration_config cfg, std::shared_ptr<chat_storage_backend> db,
											 std::string workspace_path)
	: cfg_(std::move(cfg)), db_(std::move(db)), workspace_path_(std::move(workspace_path)) {
}

// ─── should_run_auto ──────────────────────────────────────────────────────

bool self_iteration_engine::should_run_auto() const {
	if (!cfg_.enabled)
		return false;
	if (cfg_.interval_hours <= 0)
		return false;

	std::lock_guard<std::mutex> lk(mutex_);
	auto now = std::chrono::system_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - last_iteration_).count();
	return elapsed >= cfg_.interval_hours * 60;
}

// ─── run (full cycle) ─────────────────────────────────────────────────────

iteration_result self_iteration_engine::run() {
	iteration_result result;
	result.timestamp = now_iso();

	try {
		// Phase 1: Collect
		log::info("[self-iterate] Phase 1/5: collecting samples...");
		auto samples = collect_samples(kDefaultSampleCount);
		result.samples_evaluated = static_cast<int>(samples.size());

		if (samples.size() < k_min_samples) {
			result.summary = "样本不足（需要至少3条对话），跳过评估。";
			result.executed = true;
			log::info("[self-iterate] " + result.summary);
			return result;
		}

		// Phase 2: Evaluate
		log::info("[self-iterate] Phase 2/5: evaluating with Claude Code...");
		auto eval = evaluate(samples);
		result.avg_tone_score = eval.avg_tone_score;
		result.avg_accuracy_score = eval.avg_accuracy_score;
		result.avg_completeness_score = eval.avg_completeness_score;
		result.avg_efficiency_score = eval.avg_efficiency_score;
		result.issues_found = static_cast<int>(eval.issues.size());

		// Phase 3: Decide
		log::info("[self-iterate] Phase 3/5: deciding...");
		if (!should_improve(eval)) {
			result.summary = "质量评分良好，无需改进。";
			result.executed = true;
			log::info("[self-iterate] " + result.summary);
			return result;
		}

		// Phase 4: Plan improvements
		log::info("[self-iterate] Phase 4/5: planning improvements...");
		auto plan = plan_improvements(eval);

		if (plan.edits.empty()) {
			result.summary = "Claude Code 未生成任何改进方案。";
			result.executed = true;
			log::info("[self-iterate] " + result.summary);
			return result;
		}

		// Phase 5: Apply + commit
		log::info("[self-iterate] Phase 5/5: applying improvements...");
		int applied = apply_improvements(plan);
		result.improvements_applied = applied;

		if (applied > 0 && !plan.summary.empty()) {
			result.git_commit_hash = commit_changes(plan.summary);
		}

		result.summary = plan.summary.empty() ? "已应用 " + std::to_string(applied) + " 项改进。" : plan.summary;
		result.executed = true;

		std::lock_guard<std::mutex> lk(mutex_);
		last_iteration_ = std::chrono::system_clock::now();

		log::info("[self-iterate] done: " + result.summary);
	} catch (std::exception const& ex) {
		result.error = ex.what();
		log::error("[self-iterate] failed: " + std::string(ex.what()));
	} catch (...) {
		result.error = "unknown error";
		log::error("[self-iterate] failed: unknown");
	}

	return result;
}

// ─── dry_run (evaluate only) ──────────────────────────────────────────────

iteration_result self_iteration_engine::dry_run() {
	iteration_result result;
	result.timestamp = now_iso();
	result.dry_run = true;

	try {
		auto samples = collect_samples(kDefaultSampleCount);
		result.samples_evaluated = static_cast<int>(samples.size());

		if (samples.size() < k_min_samples) {
			result.summary = "样本不足（需要至少3条对话），跳过评估。";
			result.executed = true;
			return result;
		}

		auto eval = evaluate(samples);
		result.avg_tone_score = eval.avg_tone_score;
		result.avg_accuracy_score = eval.avg_accuracy_score;
		result.avg_completeness_score = eval.avg_completeness_score;
		result.avg_efficiency_score = eval.avg_efficiency_score;
		result.issues_found = static_cast<int>(eval.issues.size());

		std::ostringstream s;
		s << "dry-run 评估完成: " << result.samples_evaluated << " 条样本\n"
		  << "语气: " << result.avg_tone_score << " | 准确: " << result.avg_accuracy_score
		  << " | 完整: " << result.avg_completeness_score << " | 效率: " << result.avg_efficiency_score;
		if (!eval.issues.empty()) {
			s << "\n\n发现 " << eval.issues.size() << " 个问题:\n";
			for (auto const& iss : eval.issues)
				s << "- " << iss << "\n";
		}
		result.summary = s.str();
		result.executed = true;
	} catch (std::exception const& ex) {
		result.error = ex.what();
	} catch (...) {
		result.error = "unknown error";
	}

	return result;
}

// ─── collect_samples ──────────────────────────────────────────────────────

std::vector<conversation_sample> self_iteration_engine::collect_samples(int count) {
	auto* s = dynamic_cast<sqlite_backend*>(db_.get());
	if (!s) {
		log::warn("[self-iterate] backend is not sqlite_backend — cannot collect samples");
		return {};
	}

	auto rows = query_recent_messages(s->raw_handle(), count * 3);
	return pair_exchanges(rows);
}

// ─── evaluate ─────────────────────────────────────────────────────────────

evaluation_report self_iteration_engine::evaluate(std::vector<conversation_sample> const& samples) {
	evaluation_report report;
	report.total_samples = static_cast<int>(samples.size());

	// Build samples JSON
	auto sj = nlohmann::json::array();
	for (size_t i = 0; i < samples.size(); ++i) {
		auto const& s = samples[i];
		sj.push_back({{"index", i},
					  {"convo_id", s.convo_id},
					  {"timestamp", s.timestamp},
					  {"user_message", sanitize_utf8(s.user_message)},
					  {"bot_response", sanitize_utf8(s.bot_response)},
					  {"tool_calls", s.tool_calls}});
	}

	// Collect current guidelines
	std::string guidelines;
	guidelines += "=== PROMPT.md ===\n" + read_workspace_file(workspace_path_, "PROMPT.md") + "\n\n";
	guidelines += "=== SOUL.md ===\n" + read_workspace_file(workspace_path_, "SOUL.md") + "\n\n";
	guidelines += "=== AGENTS.md ===\n" + read_workspace_file(workspace_path_, "AGENTS.md") + "\n\n";
	guidelines += "=== MEMORY.md ===\n" + read_workspace_file(workspace_path_, "MEMORY.md") + "\n\n";

	std::string tools = read_workspace_file(workspace_path_, "TOOLS.md");

	// Build prompt
	std::string prompt = client::build_evaluate_prompt(sj.dump(2), guidelines, tools);

	// Write prompt to temp file for stdin redirection
	auto tmp_path =
		std::filesystem::temp_directory_path() /
		("bot_self_iter_eval_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".txt");
	{
		std::ofstream ofs(tmp_path);
		ofs << prompt;
	}

	// Invoke Claude Code with input redirection
	std::ostringstream cmd;
	cmd << "claude -p --output-format json < " << std::quoted(tmp_path.string());

	log::info("[self-iterate] invoking Claude Code for evaluation...");
	std::string raw = agent_reach_client::exec(cmd.str(), std::chrono::seconds(kClaudeTimeoutSeconds));

	// Clean up temp file
	try {
		std::filesystem::remove(tmp_path);
	} catch (...) {
	}

	if (raw.empty()) {
		report.issues.push_back("Claude Code 未返回评估结果（可能未安装或超时）");
		return report;
	}

	// Parse JSON
	auto clean = strip_json_fences(raw);
	report.raw_json = clean;
	auto j = nlohmann::json::parse(clean, nullptr, false);

	if (j.is_discarded()) {
		report.issues.push_back("Claude Code 返回了非JSON格式的评估结果");
		log::warn("[self-iterate] unparseable JSON: " + raw.substr(0, 200));
		return report;
	}

	// Extract scores
	auto const& overall = j["overall"];
	if (overall.is_object()) {
		report.avg_tone_score = overall.value("avg_tone", 0.0);
		report.avg_accuracy_score = overall.value("avg_accuracy", 0.0);
		report.avg_completeness_score = overall.value("avg_completeness", 0.0);
		report.avg_efficiency_score = overall.value("avg_efficiency", 0.0);
	}

	report.safety_concern = j.value("safety_concern", false);

	if (j.contains("issues") && j["issues"].is_array()) {
		for (auto const& iss : j["issues"]) {
			if (iss.is_string())
				report.issues.push_back(iss.get<std::string>());
		}
	}
	if (j.contains("suggestions") && j["suggestions"].is_array()) {
		for (auto const& sug : j["suggestions"]) {
			if (sug.is_string())
				report.suggestions.push_back(sug.get<std::string>());
		}
	}

	return report;
}

// ─── should_improve ───────────────────────────────────────────────────────

bool self_iteration_engine::should_improve(evaluation_report const& report) const {
	if (report.safety_concern)
		return true;

	double avg = (report.avg_tone_score + report.avg_accuracy_score + report.avg_completeness_score +
				  report.avg_efficiency_score) /
				 4.0;
	return avg < kImproveThreshold;
}

// ─── plan_improvements ────────────────────────────────────────────────────

improvement_plan self_iteration_engine::plan_improvements(evaluation_report const& eval) {
	improvement_plan plan;

	// Build current guidelines snapshot
	std::string guidelines;
	guidelines += "=== PROMPT.md ===\n" + read_workspace_file(workspace_path_, "PROMPT.md") + "\n\n";
	guidelines += "=== SOUL.md ===\n" + read_workspace_file(workspace_path_, "SOUL.md") + "\n\n";
	guidelines += "=== AGENTS.md ===\n" + read_workspace_file(workspace_path_, "AGENTS.md") + "\n\n";
	guidelines += "=== TOOLS.md ===\n" + read_workspace_file(workspace_path_, "TOOLS.md") + "\n\n";
	guidelines += "=== MEMORY.md ===\n" + read_workspace_file(workspace_path_, "MEMORY.md") + "\n\n";
	guidelines += "=== USER.md ===\n" + read_workspace_file(workspace_path_, "USER.md") + "\n\n";

	// Build a brief sample summary for context
	std::string sample_summary = "Issues found:\n";
	for (auto const& iss : eval.issues)
		sample_summary += "- " + iss + "\n";
	sample_summary += "\nSuggestions:\n";
	for (auto const& sug : eval.suggestions)
		sample_summary += "- " + sug + "\n";

	// Build prompt
	std::string prompt = client::build_improve_prompt(eval.raw_json, guidelines, sample_summary);

	// Write to temp file
	auto tmp_path = std::filesystem::temp_directory_path() /
					("bot_self_iter_improve_" +
					 std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".txt");
	{
		std::ofstream ofs(tmp_path);
		ofs << prompt;
	}

	log::info("[self-iterate] invoking Claude Code for improvement plan...");
	std::ostringstream improve_cmd;
	improve_cmd << "claude -p --output-format json < " << std::quoted(tmp_path.string());
	std::string raw = agent_reach_client::exec(improve_cmd.str(), std::chrono::seconds(kClaudeTimeoutSeconds));

	try {
		std::filesystem::remove(tmp_path);
	} catch (...) {
	}

	if (raw.empty()) {
		plan.summary = "Claude Code 未返回改进方案";
		return plan;
	}

	auto clean = strip_json_fences(raw);
	auto j = nlohmann::json::parse(clean, nullptr, false);

	if (j.is_discarded()) {
		log::warn("[self-iterate] unparseable improvement JSON: " + raw.substr(0, 200));
		return plan;
	}

	plan.summary = j.value("summary", "");
	plan.detailed_rationale = j.value("rationale", "");

	if (j.contains("edits") && j["edits"].is_array()) {
		for (auto const& e : j["edits"]) {
			file_edit fe;
			fe.path = e.value("file", "");
			fe.old_content = e.value("old_text", "");
			fe.new_content = e.value("new_text", "");
			fe.reason = e.value("reason", "");

			if (!is_editable_file(fe.path)) {
				log::warn("[self-iterate] blocked edit to non-whitelist file: " + fe.path);
				continue;
			}
			plan.edits.push_back(std::move(fe));
		}
	}

	return plan;
}

// ─── apply_improvements ───────────────────────────────────────────────────

int self_iteration_engine::apply_improvements(improvement_plan const& plan) {
	int applied = 0;

	for (auto const& edit : plan.edits) {
		if (!is_editable_file(edit.path)) {
			log::warn("[self-iterate] skipping non-whitelisted file: " + edit.path);
			continue;
		}

		std::filesystem::path fp = std::filesystem::path(workspace_path_) / edit.path;
		std::string content = read_file(fp.string());

		if (edit.old_content == "<<APPEND>>") {
			// Append mode
			content += "\n" + edit.new_content;
			if (write_file(fp.string(), content)) {
				++applied;
				log::info("[self-iterate] appended to " + edit.path + ": " + edit.reason);
			}
			continue;
		}

		// Replace mode: find old_text and replace with new_text
		auto pos = content.find(edit.old_content);
		if (pos == std::string::npos) {
			log::warn("[self-iterate] old_text not found in " + edit.path + " — skipping edit: " + edit.reason);
			continue;
		}

		content.replace(pos, edit.old_content.size(), edit.new_content);
		if (write_file(fp.string(), content)) {
			++applied;
			log::info("[self-iterate] edited " + edit.path + ": " + edit.reason);
		}
	}

	return applied;
}

// ─── commit_changes ───────────────────────────────────────────────────────

std::string self_iteration_engine::commit_changes(std::string const& summary) {
	// Build the list of workspace files for git add
	std::string workspace_pattern = workspace_path_;
	// Normalize path separators for git
	for (auto& ch : workspace_pattern)
		if (ch == '\\')
			ch = '/';
	if (!workspace_pattern.empty() && workspace_pattern.back() != '/')
		workspace_pattern += '/';

	std::string add_cmd = "git add " + workspace_pattern + "*.md";
	std::string result = agent_reach_client::exec(add_cmd);

	if (result.find("fatal:") != std::string::npos) {
		log::warn("[self-iterate] git add failed: " + result);
	}

	// Also add bot_config.json if modified
	agent_reach_client::exec("git add bot_config.json");

	// Build commit message
	std::string commit_msg = "self-iterate: " + summary;
	std::string commit_cmd = "git commit -m " + shell_quote(commit_msg);

	result = agent_reach_client::exec(commit_cmd);
	if (result.find("fatal:") != std::string::npos && result.find("nothing to commit") == std::string::npos) {
		log::warn("[self-iterate] git commit failed: " + result);
	}

	// Extract commit hash
	std::string hash_out = agent_reach_client::exec("git rev-parse --short HEAD");
	hash_out = client::trim(hash_out);

	log::info("[self-iterate] committed: " + hash_out + " — " + commit_msg);
	return hash_out;
}

// ─── helpers ──────────────────────────────────────────────────────────────

std::string self_iteration_engine::read_file(std::string const& path) {
	std::ifstream in(path);
	if (!in)
		return {};
	std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	return sanitize_utf8(content);
}

bool self_iteration_engine::write_file(std::string const& path, std::string const& content) {
	try {
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out)
			return false;
		out << content;
		return out.good();
	} catch (...) {
		return false;
	}
}

std::string self_iteration_engine::read_workspace_file(std::string const& ws_dir, std::string const& filename) {
	std::filesystem::path p = std::filesystem::path(ws_dir) / filename;
	if (!std::filesystem::exists(p))
		return {};
	return read_file(p.string());
}

std::string self_iteration_engine::shell_quote(std::string_view s) {
	// Simple double-quote escaping — avoids std::quoted() which returns a
	// stream manipulator, not a string.
	std::string r;
	r.reserve(s.size() + 2);
	r += '"';
	for (char ch : s) {
		if (ch == '"' || ch == '\\' || ch == '$' || ch == '`')
			r += '\\';
		r += ch;
	}
	r += '"';
	return r;
}

} // namespace client
