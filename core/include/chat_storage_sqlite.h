/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "chat_context_manager.h"
#include "log.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <ctime>
#include <stdexcept>
#include <string>
#include <vector>

namespace client {

// ─── SQLite storage backend (row-based) ──────────────────────────────────
//
// Schema v2:
//   messages (id, convo_id, role, nick, content, tool_call_id, tool_calls_json, created_at)
//     └─ INDEX idx_convo_time ON (convo_id, created_at)
//
// Role CHECK allows 'user','assistant','system','tool'.
// Each message is one row — append is O(1), no full-table JSON rewrite.
// WAL mode + busy_timeout for concurrent readers under chat_context_manager::mutex_.

struct sqlite_backend : chat_storage_backend {
	explicit sqlite_backend(std::string db_path) : db_path_(std::move(db_path)) {
		open_db();
		exec("PRAGMA journal_mode=WAL;");
		exec("PRAGMA busy_timeout=3000;");
		exec("PRAGMA foreign_keys=OFF;");
		exec("CREATE TABLE IF NOT EXISTS messages ("
			 "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
			 "  convo_id   TEXT    NOT NULL,"
			 "  role       TEXT    NOT NULL CHECK (role IN ('user','assistant','system','tool')),"
			 "  nick       TEXT    NOT NULL DEFAULT '',"
			 "  content    TEXT    NOT NULL,"
			 "  tool_call_id TEXT  NOT NULL DEFAULT '',"
			 "  tool_calls_json TEXT NOT NULL DEFAULT '',"
			 "  created_at INTEGER NOT NULL DEFAULT (unixepoch())"
			 ");");
		migrate_schema();
		exec("CREATE INDEX IF NOT EXISTS idx_convo_time "
			 "ON messages(convo_id, created_at);");
		// Drop legacy table if it exists from previous schema
		exec("DROP TABLE IF EXISTS conversations;");

		// Token usage tracking
		exec("CREATE TABLE IF NOT EXISTS token_usage ("
			 "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
			 "  created_at    INTEGER NOT NULL DEFAULT (unixepoch()),"
			 "  model         TEXT    NOT NULL DEFAULT '',"
			 "  prompt_tokens INTEGER NOT NULL DEFAULT 0,"
			 "  completion_tokens INTEGER NOT NULL DEFAULT 0"
			 ");");
		exec("CREATE INDEX IF NOT EXISTS idx_token_day "
			 "ON token_usage(created_at);");
	}

	~sqlite_backend() override {
		if (db_)
			sqlite3_close(db_);
	}

	sqlite_backend(sqlite_backend const&) = delete;
	sqlite_backend& operator=(sqlite_backend const&) = delete;

	// ── load ─────────────────────────────────────────────────────────

	std::vector<chat_message> load(std::string const& convo_id) override {
		std::vector<chat_message> msgs;
		if (!db_)
			return msgs;

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(db_,
							   "SELECT role, nick, content, tool_call_id, tool_calls_json FROM messages "
							   "WHERE convo_id = ?1 ORDER BY created_at, id;",
							   -1, &stmt, nullptr) != SQLITE_OK) {
			log::error(std::string("[sqlite] load prepare failed: ") + sqlite3_errmsg(db_));
			return msgs;
		}

		sqlite3_bind_text(stmt, 1, convo_id.c_str(), -1, SQLITE_STATIC);
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			chat_message m;
			m.role = str_col(stmt, 0);
			m.sender_nick = str_col(stmt, 1);
			m.content = str_col(stmt, 2);
			m.tool_call_id = str_col(stmt, 3);
			m.tool_calls_json = str_col(stmt, 4);
			msgs.push_back(std::move(m));
		}
		sqlite3_finalize(stmt);
		return msgs;
	}

	// ── save ─────────────────────────────────────────────────────────
	// Replaces ALL messages for convo_id in a single transaction.

	void save(std::string const& convo_id, std::vector<chat_message> const& messages) override {
		if (!db_)
			return;

		exec("BEGIN IMMEDIATE;");

		// Delete old rows
		sqlite3_stmt* del = nullptr;
		if (sqlite3_prepare_v2(db_, "DELETE FROM messages WHERE convo_id = ?1;", -1, &del, nullptr) != SQLITE_OK) {
			log::error(std::string("[sqlite] save delete-prepare failed: ") + sqlite3_errmsg(db_));
			exec("ROLLBACK;");
			return;
		}
		sqlite3_bind_text(del, 1, convo_id.c_str(), -1, SQLITE_STATIC);
		sqlite3_step(del);
		sqlite3_finalize(del);

		// Insert new rows
		if (!messages.empty()) {
			sqlite3_stmt* ins = nullptr;
			if (sqlite3_prepare_v2(
					db_,
					"INSERT INTO messages (convo_id, role, nick, content, tool_call_id, tool_calls_json, created_at) "
					"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7);",
					-1, &ins, nullptr) != SQLITE_OK) {
				log::error(std::string("[sqlite] save insert-prepare failed: ") + sqlite3_errmsg(db_));
				exec("ROLLBACK;");
				return;
			}

			auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
						   std::chrono::system_clock::now().time_since_epoch())
						   .count();
			for (size_t i = 0; i < messages.size(); ++i) {
				auto const& m = messages[i];
				sqlite3_reset(ins);
				sqlite3_bind_text(ins, 1, convo_id.c_str(), -1, SQLITE_STATIC);
				sqlite3_bind_text(ins, 2, m.role.c_str(), -1, SQLITE_STATIC);
				sqlite3_bind_text(ins, 3, m.sender_nick.c_str(), -1, SQLITE_STATIC);
				sqlite3_bind_text(ins, 4, m.content.c_str(), -1, SQLITE_STATIC);
				sqlite3_bind_text(ins, 5, m.tool_call_id.c_str(), -1, SQLITE_STATIC);
				sqlite3_bind_text(ins, 6, m.tool_calls_json.c_str(), -1, SQLITE_STATIC);
				sqlite3_bind_int64(ins, 7, now + static_cast<int64_t>(i)); // preserve order within batch
				sqlite3_step(ins);
			}
			sqlite3_finalize(ins);
		}

		exec("COMMIT;");
	}

	// ── append_message ────────────────────────────────────────────────
	// O(1) single-row insert — no load->modify->save cycle needed.

	void append_message(std::string const& convo_id, chat_message const& m) override {
		if (!db_)
			return;

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(
				db_,
				"INSERT INTO messages (convo_id, role, nick, content, tool_call_id, tool_calls_json, created_at) "
				"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7);",
				-1, &stmt, nullptr) != SQLITE_OK) {
			log::error(std::string("[sqlite] append prepare failed: ") + sqlite3_errmsg(db_));
			return;
		}

		sqlite3_bind_text(stmt, 1, convo_id.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, m.role.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 3, m.sender_nick.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 4, m.content.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 5, m.tool_call_id.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 6, m.tool_calls_json.c_str(), -1, SQLITE_STATIC);
		auto now_ms =
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
				.count();
		sqlite3_bind_int64(stmt, 7, now_ms);

		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}

	// ── clear_messages ────────────────────────────────────────────────

	void clear_messages(std::string const& convo_id) override {
		if (!db_)
			return;
		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(db_, "DELETE FROM messages WHERE convo_id = ?1;", -1, &stmt, nullptr) != SQLITE_OK) {
			log::error(std::string("[sqlite] clear prepare failed: ") + sqlite3_errmsg(db_));
			return;
		}
		sqlite3_bind_text(stmt, 1, convo_id.c_str(), -1, SQLITE_STATIC);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}

	// ── count ─────────────────────────────────────────────────────────

	int64_t count(std::string const& convo_id) override {
		if (!db_)
			return 0;
		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM messages WHERE convo_id = ?1;", -1, &stmt, nullptr) !=
			SQLITE_OK) {
			log::error(std::string("[sqlite] count prepare failed: ") + sqlite3_errmsg(db_));
			return 0;
		}
		sqlite3_bind_text(stmt, 1, convo_id.c_str(), -1, SQLITE_STATIC);
		int64_t n = 0;
		if (sqlite3_step(stmt) == SQLITE_ROW)
			n = sqlite3_column_int64(stmt, 0);
		sqlite3_finalize(stmt);
		return n;
	}

	// ── token usage tracking ────────────────────────────────────────

	void record_token_usage(std::string const& model, int prompt_tokens, int completion_tokens) {
		if (!db_)
			return;
		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(db_,
							   "INSERT INTO token_usage (model, prompt_tokens, completion_tokens) "
							   "VALUES (?1, ?2, ?3);",
							   -1, &stmt, nullptr) != SQLITE_OK) {
			log::error(std::string("[sqlite] token insert failed: ") + sqlite3_errmsg(db_));
			return;
		}
		sqlite3_bind_text(stmt, 1, model.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_int(stmt, 2, prompt_tokens);
		sqlite3_bind_int(stmt, 3, completion_tokens);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}

	// Return daily stats for the last 30 days ordered by date desc.
	// Each row: date, requests, prompt_tokens, completion_tokens
	std::vector<std::tuple<std::string, int, int64_t, int64_t>> get_token_stats() {
		std::vector<std::tuple<std::string, int, int64_t, int64_t>> rows;
		if (!db_)
			return rows;

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(db_,
							   "SELECT "
							   "  date(created_at, 'unixepoch', 'localtime'),"
							   "  COUNT(*),"
							   "  SUM(prompt_tokens),"
							   "  SUM(completion_tokens) "
							   "FROM token_usage "
							   "WHERE created_at > unixepoch() - 2592000 " // last 30 days
							   "GROUP BY date(created_at, 'unixepoch', 'localtime') "
							   "ORDER BY date(created_at, 'unixepoch', 'localtime') DESC;",
							   -1, &stmt, nullptr) != SQLITE_OK) {
			log::error(std::string("[sqlite] token stats failed: ") + sqlite3_errmsg(db_));
			return rows;
		}

		while (sqlite3_step(stmt) == SQLITE_ROW) {
			std::string date = str_col(stmt, 0);
			int count = static_cast<int>(sqlite3_column_int64(stmt, 1));
			int64_t prompt = sqlite3_column_int64(stmt, 2);
			int64_t completion = sqlite3_column_int64(stmt, 3);
			rows.emplace_back(date, count, prompt, completion);
		}
		sqlite3_finalize(stmt);
		return rows;
	}

	// Direct handle access for cross-table queries (self_iteration).
	sqlite3* raw_handle() {
		return db_;
	}

	private:
	std::string db_path_;
	sqlite3* db_ = nullptr;

	void open_db() {
		int rc = sqlite3_open_v2(db_path_.c_str(), &db_,
								 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
		if (rc != SQLITE_OK) {
			auto msg = std::string(sqlite3_errstr(rc));
			if (db_) {
				sqlite3_close(db_);
				db_ = nullptr;
			}
			throw std::runtime_error("sqlite open failed: " + msg);
		}
	}

	void exec(char const* sql) {
		char* err = nullptr;
		sqlite3_exec(db_, sql, nullptr, nullptr, &err);
		if (err) {
			log::error(std::string("[sqlite] exec error: ") + err + " (sql: " + sql + ")");
			sqlite3_free(err);
		}
	}

	// ─── schema migration ──────────────────────────────────────────────
	// Detects old schema (missing tool_call_id column) and recreates the
	// table with the new columns + relaxed role CHECK constraint.
	// Wrapped in a transaction for safety.

	void migrate_schema() {
		// Check if tool_call_id column exists
		bool has_tool_call_id = false;
		{
			sqlite3_stmt* stmt = nullptr;
			if (sqlite3_prepare_v2(db_, "PRAGMA table_info(messages);", -1, &stmt, nullptr) == SQLITE_OK) {
				while (sqlite3_step(stmt) == SQLITE_ROW) {
					if (str_col(stmt, 1) == "tool_call_id") {
						has_tool_call_id = true;
						break;
					}
				}
			}
			sqlite3_finalize(stmt);
		}

		if (has_tool_call_id)
			return; // already migrated

		log::info("[sqlite] migrating messages table to v2 schema...");

		exec("BEGIN;");
		exec("CREATE TABLE messages_v2 ("
			 "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
			 "  convo_id   TEXT    NOT NULL,"
			 "  role       TEXT    NOT NULL CHECK (role IN ('user','assistant','system','tool')),"
			 "  nick       TEXT    NOT NULL DEFAULT '',"
			 "  content    TEXT    NOT NULL,"
			 "  tool_call_id TEXT  NOT NULL DEFAULT '',"
			 "  tool_calls_json TEXT NOT NULL DEFAULT '',"
			 "  created_at INTEGER NOT NULL DEFAULT (unixepoch())"
			 ");");
		exec("INSERT INTO messages_v2 (id, convo_id, role, nick, content, tool_call_id, tool_calls_json, created_at) "
			 "SELECT id, convo_id, role, nick, content, '', '', created_at FROM messages;");
		exec("DROP TABLE messages;");
		exec("ALTER TABLE messages_v2 RENAME TO messages;");
		exec("COMMIT;");

		log::info("[sqlite] schema migration complete");
	}

	static std::string str_col(sqlite3_stmt* stmt, int col) {
		auto const* text = reinterpret_cast<char const*>(sqlite3_column_text(stmt, col));
		return text ? std::string(text) : std::string{};
	}
};

} // namespace client
