/**
 * Auth SQLite database initialisation.
 *
 * Uses better-sqlite3 (already a project dependency) for synchronous,
 * read-your-writes access.  The auth database is separate from per-agent
 * conversations.db — it stores user accounts and OAuth platform links.
 *
 * Resource pattern: callers MUST NOT hold the returned Database handle
 * across requests.  Each request should obtain a fresh handle via
 * getAuthDb() and close it in try/finally — see CLAUDE.md line 185-195.
 */

import Database from 'better-sqlite3';
import fs from 'fs';
import path from 'path';
import { config } from './config';

let _db: Database.Database | null = null;

/** Return the singleton auth Database handle, creating tables on first call. */
export function getAuthDb(): Database.Database {
  if (_db) return _db;

  const dir = path.dirname(config.authDbPath);
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
  }

  _db = new Database(config.authDbPath);
  _db.pragma('journal_mode = WAL');
  _db.pragma('foreign_keys = ON');
  _db.pragma('busy_timeout = 5000');

  _db.exec(`
    CREATE TABLE IF NOT EXISTS users (
      id          TEXT PRIMARY KEY,
      username    TEXT NOT NULL UNIQUE,
      avatar_url  TEXT NOT NULL DEFAULT '',
      created_at  TEXT NOT NULL DEFAULT (datetime('now'))
    );

    CREATE TABLE IF NOT EXISTS oauth_accounts (
      id                TEXT PRIMARY KEY,
      user_id           TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      provider          TEXT NOT NULL CHECK(provider IN ('github', 'qq')),
      provider_user_id  TEXT NOT NULL,
      provider_username TEXT NOT NULL DEFAULT '',
      access_token      TEXT NOT NULL DEFAULT '',
      refresh_token     TEXT NOT NULL DEFAULT '',
      token_expires_at  TEXT DEFAULT NULL,
      created_at        TEXT NOT NULL DEFAULT (datetime('now')),
      UNIQUE(provider, provider_user_id)
    );

    CREATE TABLE IF NOT EXISTS credentials (
      user_id       TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
      password_hash TEXT NOT NULL,
      updated_at    TEXT NOT NULL DEFAULT (datetime('now'))
    );

    CREATE INDEX IF NOT EXISTS idx_oauth_user
      ON oauth_accounts(user_id);
    CREATE INDEX IF NOT EXISTS idx_oauth_provider_user
      ON oauth_accounts(provider, provider_user_id);
  `);

  return _db;
}

/** Release the database handle (useful for tests). */
export function closeAuthDb(): void {
  if (_db) {
    _db.close();
    _db = null;
  }
}
