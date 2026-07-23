/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "chat_context_manager.h"

#include <nlohmann/json_fwd.hpp>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace client {

// ─── self_iteration_config ─────────────────────────────────────────────────
// Loaded from bot_config.json; controls the self-iteration loop.

struct self_iteration_config {
	bool enabled = false;
	int interval_hours = 24;			// auto-trigger interval (0 = disabled)
	int min_conversations = 10;			// minimum active convos before auto-trigger
	std::string claude_path = "claude"; // Claude Code CLI executable name or path
};

// ─── conversation_sample ──────────────────────────────────────────────────
// A single user→bot exchange extracted from the chat store for evaluation.

struct conversation_sample {
	std::string convo_id;
	std::string timestamp;
	std::string user_message;
	std::string bot_response;
	int tool_calls = 0; // how many tools were used in this exchange
};

// ─── evaluation_report ────────────────────────────────────────────────────
// Structured result from Claude Code's quality evaluation.

struct evaluation_report {
	double avg_tone_score = 0;		   // 1-5: matches SOUL.md?
	double avg_accuracy_score = 0;	   // 1-5: factually correct?
	double avg_completeness_score = 0; // 1-5: fully answers user?
	double avg_efficiency_score = 0;   // 1-5: minimal effective tool use?
	int total_samples = 0;
	bool safety_concern = false;
	std::string raw_json;				  // full Claude Code response (for debugging)
	std::vector<std::string> issues;	  // human-readable issue descriptions
	std::vector<std::string> suggestions; // improvement suggestions
};

// ─── file_edit ────────────────────────────────────────────────────────────
// A single file modification proposed by Claude Code.

struct file_edit {
	std::string path;		 // relative to workspace dir
	std::string old_content; // text to replace (empty = insert at top)
	std::string new_content; // replacement text (empty = delete)
	std::string reason;		 // why this edit is needed
};

// ─── improvement_plan ─────────────────────────────────────────────────────
// A set of file edits + a human-readable summary.

struct improvement_plan {
	std::string summary;			// one-line description
	std::string detailed_rationale; // longer explanation
	std::vector<file_edit> edits;
};

// ─── iteration_result ─────────────────────────────────────────────────────
// Returned by run() / dry_run(); serializable for logging.

struct iteration_result {
	bool executed = false;
	bool dry_run = false;
	std::string timestamp;
	std::string summary;
	double avg_tone_score = 0;
	double avg_accuracy_score = 0;
	double avg_completeness_score = 0;
	double avg_efficiency_score = 0;
	int samples_evaluated = 0;
	int issues_found = 0;
	int improvements_applied = 0;
	std::string git_commit_hash;
	std::string error; // non-empty if something failed
};

// ─── self_iteration_engine ──────────────────────────────────────────────────
//
// LLM-driven self-improvement loop powered by Claude Code CLI.
//
// Usage:
//   self_iteration_engine engine(cfg, db_backend, workspace_path);
//   auto result = engine.run();       // full cycle: collect→evaluate→improve→apply
//   auto result = engine.dry_run();   // evaluate only, no file changes
//
// The engine invokes `claude -p` (one-shot mode) twice per cycle:
//   1. EVALUATE  — rate recent bot replies across 4 quality dimensions
//   2. IMPROVE   — propose concrete file edits to workspace/*.md
//
// Safety: only files under the workspace directory and bot_config.json
// may be modified.  C++ source files are never touched by self-iteration.

class self_iteration_engine {
	public:
	self_iteration_engine(self_iteration_config cfg, std::shared_ptr<chat_storage_backend> db,
						  std::string workspace_path);

	// ── public API ──────────────────────────────────────────────────────

	// Full cycle: collect → evaluate → plan → apply → commit.
	// Returns after all phases complete (may take 30-120 s).
	iteration_result run();

	// Evaluate only — no file modifications, no git commit.
	iteration_result dry_run();

	// Last successful iteration timestamp (for interval gating).
	std::chrono::system_clock::time_point last_iteration_at() const {
		std::lock_guard<std::mutex> lk(mutex_);
		return last_iteration_;
	}

	// Quick check: should an auto-triggered iteration run now?
	bool should_run_auto() const;

	private:
	// ── Phase 1: Collect ───────────────────────────────────────────────
	// Pull recent (user + assistant) pairs from the chat store.
	std::vector<conversation_sample> collect_samples(int count = 10);

	// ── Phase 2: Evaluate ──────────────────────────────────────────────
	// Build the evaluation prompt, invoke Claude Code, parse JSON response.
	evaluation_report evaluate(std::vector<conversation_sample> const& samples);

	// ── Phase 3: Decide ────────────────────────────────────────────────
	// Determine whether the scores justify triggering Phase 4/5.
	bool should_improve(evaluation_report const& report) const;

	// ── Phase 4: Improve ───────────────────────────────────────────────
	// Ask Claude Code to propose concrete file edits.
	improvement_plan plan_improvements(evaluation_report const& eval);

	// ── Phase 5: Apply & Commit ────────────────────────────────────────
	// Write edits to disk + git commit.
	int apply_improvements(improvement_plan const& plan);
	std::string commit_changes(std::string const& summary);

	// ── helpers ────────────────────────────────────────────────────────
	static std::string read_file(std::string const& path);
	static bool write_file(std::string const& path, std::string const& content);
	static std::string read_workspace_file(std::string const& ws_dir, std::string const& filename);
	static std::string exec(std::string_view cmd, std::chrono::seconds timeout = std::chrono::seconds(120));
	static std::string shell_quote(std::string_view s);
	self_iteration_config cfg_;
	std::shared_ptr<chat_storage_backend> db_;
	std::string workspace_path_;
	mutable std::mutex mutex_;
	std::chrono::system_clock::time_point last_iteration_{};
};

// ─── shared factory (used by agent_registry and main) ───────────────────
// Builds a self-iteration callback suitable for assigning to
// agent_controller::on_self_iterate.  Returns an empty function if si is null.
std::function<std::string(bool)> make_si_callback(std::shared_ptr<self_iteration_engine> si);

} // namespace client
