/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "log.h"
#include "plugin_config_backend.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <stdexcept>
#include <string>

namespace client {

// ─── plugin_config_sqlite ────────────────────────────────────────────────
// SQLite-backed plugin configuration storage.
//
// Single database file (<storage_dir>/plugin_config.db), two tables:
//   agent_plugins  — per-agent enable/disable + config_json
//   user_plugins   — per-user  enable/disable
//
// Thread-safe: protected by internal mutex (all public methods lock it).
// Write-through: setter methods write to SQLite immediately — callers
// that also maintain an in-memory cache (plugin_manager) should update
// their cache after every successful write.

struct plugin_config_sqlite : plugin_config_backend {
	explicit plugin_config_sqlite(std::string db_path) : db_path_(std::move(db_path)) {
		open_db();
		exec("PRAGMA journal_mode=WAL;");
		exec("PRAGMA busy_timeout=3000;");
		exec("PRAGMA foreign_keys=OFF;");

		exec("CREATE TABLE IF NOT EXISTS agent_plugins ("
			 "  agent_id    TEXT NOT NULL,"
			 "  plugin_name TEXT NOT NULL,"
			 "  enabled     INTEGER NOT NULL DEFAULT 1,"
			 "  config_json TEXT NOT NULL DEFAULT '{}',"
			 "  updated_at  TEXT NOT NULL DEFAULT (datetime('now')),"
			 "  PRIMARY KEY (agent_id, plugin_name)"
			 ");");

		exec("CREATE TABLE IF NOT EXISTS user_plugins ("
			 "  user_id     TEXT NOT NULL,"
			 "  plugin_name TEXT NOT NULL,"
			 "  enabled     INTEGER NOT NULL DEFAULT 1,"
			 "  config_json TEXT NOT NULL DEFAULT '{}',"
			 "  updated_at  TEXT NOT NULL DEFAULT (datetime('now')),"
			 "  PRIMARY KEY (user_id, plugin_name)"
			 ");");
	}

	~plugin_config_sqlite() override {
		std::lock_guard<std::mutex> lock(mutex_);
		if (db_)
			sqlite3_close(db_);
	}

	plugin_config_sqlite(plugin_config_sqlite const&) = delete;
	plugin_config_sqlite& operator=(plugin_config_sqlite const&) = delete;

	// ── Agent-level ────────────────────────────────────────────────────

	bool is_plugin_enabled_for_agent(std::string const& agent_id,
									 std::string const& plugin_name) override {
		std::lock_guard<std::mutex> lock(mutex_);
		return query_enabled_agent(agent_id, plugin_name, true);
	}

	void set_plugin_enabled_for_agent(std::string const& agent_id,
									  std::string const& plugin_name, bool enabled) override {
		std::lock_guard<std::mutex> lock(mutex_);
		upsert_agent(agent_id, plugin_name, enabled, nullptr);
	}

	nlohmann::json get_plugin_config_for_agent(std::string const& agent_id,
											   std::string const& plugin_name) override {
		std::lock_guard<std::mutex> lock(mutex_);
		return query_config_agent(agent_id, plugin_name);
	}

	void set_plugin_config_for_agent(std::string const& agent_id,
									 std::string const& plugin_name,
									 nlohmann::json const& config) override {
		std::lock_guard<std::mutex> lock(mutex_);
		bool current = query_enabled_agent(agent_id, plugin_name, true);
		upsert_agent(agent_id, plugin_name, current, &config);
	}

	// ── User-level ─────────────────────────────────────────────────────

	bool is_plugin_enabled_for_user(std::string const& user_id,
									std::string const& plugin_name) override {
		std::lock_guard<std::mutex> lock(mutex_);
		return query_enabled_user(user_id, plugin_name, true);
	}

	void set_plugin_enabled_for_user(std::string const& user_id,
									 std::string const& plugin_name, bool enabled) override {
		std::lock_guard<std::mutex> lock(mutex_);
		upsert_user(user_id, plugin_name, enabled);
	}

	// ── Batch queries ──────────────────────────────────────────────────

	std::vector<std::tuple<std::string, bool, std::string>>
	list_agent_plugins(std::string const& agent_id) override {
		std::lock_guard<std::mutex> lock(mutex_);
		std::vector<std::tuple<std::string, bool, std::string>> rows;
		if (!db_)
			return rows;

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(db_,
							   "SELECT plugin_name, enabled, config_json FROM agent_plugins "
							   "WHERE agent_id = ?1 ORDER BY plugin_name;",
							   -1, &stmt, nullptr) != SQLITE_OK) {
			log::error(std::string("[plugin_config] list prepare failed: ") + sqlite3_errmsg(db_));
			return rows;
		}

		sqlite3_bind_text(stmt, 1, agent_id.c_str(), -1, SQLITE_STATIC);
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			std::string name = str_col(stmt, 0);
			bool enabled = sqlite3_column_int(stmt, 1) != 0;
			std::string cfg = str_col(stmt, 2);
			rows.emplace_back(std::move(name), enabled, std::move(cfg));
		}
		sqlite3_finalize(stmt);
		return rows;
	}

	private:
	std::string db_path_;
	sqlite3* db_ = nullptr;
	mutable std::mutex mutex_;

	void open_db() {
		int rc = sqlite3_open_v2(db_path_.c_str(), &db_,
								 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
		if (rc != SQLITE_OK) {
			auto msg = std::string(sqlite3_errstr(rc));
			if (db_) {
				sqlite3_close(db_);
				db_ = nullptr;
			}
			throw std::runtime_error("[plugin_config] sqlite open failed: " + msg);
		}
	}

	void exec(char const* sql) {
		char* err = nullptr;
		sqlite3_exec(db_, sql, nullptr, nullptr, &err);
		if (err) {
			log::error(std::string("[plugin_config] exec error: ") + err + " (sql: " + sql + ")");
			sqlite3_free(err);
		}
	}

	static std::string str_col(sqlite3_stmt* stmt, int col) {
		auto const* text = reinterpret_cast<char const*>(sqlite3_column_text(stmt, col));
		return text ? std::string(text) : std::string{};
	}

	// ── Internal helpers (caller holds mutex_) ─────────────────────────

	bool query_enabled_agent(std::string const& agent_id, std::string const& plugin_name,
							 bool fallback) {
		if (!db_)
			return fallback;

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(db_,
							   "SELECT enabled FROM agent_plugins WHERE agent_id = ?1 AND plugin_name = ?2;",
							   -1, &stmt, nullptr) != SQLITE_OK) {
			log::error(std::string("[plugin_config] query prepare failed: ") + sqlite3_errmsg(db_));
			return fallback;
		}

		sqlite3_bind_text(stmt, 1, agent_id.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, plugin_name.c_str(), -1, SQLITE_STATIC);

		bool result = fallback;
		if (sqlite3_step(stmt) == SQLITE_ROW)
			result = sqlite3_column_int(stmt, 0) != 0;
		sqlite3_finalize(stmt);
		return result;
	}

	bool query_enabled_user(std::string const& user_id, std::string const& plugin_name,
							bool fallback) {
		if (!db_)
			return fallback;

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(db_,
							   "SELECT enabled FROM user_plugins WHERE user_id = ?1 AND plugin_name = ?2;",
							   -1, &stmt, nullptr) != SQLITE_OK) {
			log::error(std::string("[plugin_config] query prepare failed: ") + sqlite3_errmsg(db_));
			return fallback;
		}

		sqlite3_bind_text(stmt, 1, user_id.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, plugin_name.c_str(), -1, SQLITE_STATIC);

		bool result = fallback;
		if (sqlite3_step(stmt) == SQLITE_ROW)
			result = sqlite3_column_int(stmt, 0) != 0;
		sqlite3_finalize(stmt);
		return result;
	}

	nlohmann::json query_config_agent(std::string const& agent_id, std::string const& plugin_name) {
		if (!db_)
			return nlohmann::json::object();

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(db_,
							   "SELECT config_json FROM agent_plugins WHERE agent_id = ?1 AND plugin_name = ?2;",
							   -1, &stmt, nullptr) != SQLITE_OK) {
			log::error(std::string("[plugin_config] config query failed: ") + sqlite3_errmsg(db_));
			return nlohmann::json::object();
		}

		sqlite3_bind_text(stmt, 1, agent_id.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, plugin_name.c_str(), -1, SQLITE_STATIC);

		nlohmann::json result = nlohmann::json::object();
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			auto cfg_str = str_col(stmt, 0);
			if (!cfg_str.empty()) {
				auto parsed = nlohmann::json::parse(cfg_str, nullptr, false);
				if (!parsed.is_discarded() && parsed.is_object())
					result = std::move(parsed);
			}
		}
		sqlite3_finalize(stmt);
		return result;
	}

	void upsert_agent(std::string const& agent_id, std::string const& plugin_name,
					  bool enabled, nlohmann::json const* config) {
		if (!db_)
			return;

		std::string cfg_json = config ? config->dump() : "{}";

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(db_,
							   "INSERT INTO agent_plugins (agent_id, plugin_name, enabled, config_json, updated_at) "
							   "VALUES (?1, ?2, ?3, ?4, datetime('now')) "
							   "ON CONFLICT(agent_id, plugin_name) DO UPDATE SET "
							   "  enabled    = excluded.enabled,"
							   "  config_json = CASE WHEN ?5 THEN excluded.config_json ELSE config_json END,"
							   "  updated_at  = datetime('now');",
							   -1, &stmt, nullptr) != SQLITE_OK) {
			log::error(std::string("[plugin_config] upsert agent failed: ") + sqlite3_errmsg(db_));
			return;
		}

		sqlite3_bind_text(stmt, 1, agent_id.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, plugin_name.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_int(stmt, 3, enabled ? 1 : 0);
		sqlite3_bind_text(stmt, 4, cfg_json.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_int(stmt, 5, config ? 1 : 0); // only overwrite config when explicitly provided

		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}

	void upsert_user(std::string const& user_id, std::string const& plugin_name, bool enabled) {
		if (!db_)
			return;

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(db_,
							   "INSERT INTO user_plugins (user_id, plugin_name, enabled, updated_at) "
							   "VALUES (?1, ?2, ?3, datetime('now')) "
							   "ON CONFLICT(user_id, plugin_name) DO UPDATE SET "
							   "  enabled    = excluded.enabled,"
							   "  updated_at = datetime('now');",
							   -1, &stmt, nullptr) != SQLITE_OK) {
			log::error(std::string("[plugin_config] upsert user failed: ") + sqlite3_errmsg(db_));
			return;
		}

		sqlite3_bind_text(stmt, 1, user_id.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, plugin_name.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_int(stmt, 3, enabled ? 1 : 0);

		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}
};

} // namespace client
