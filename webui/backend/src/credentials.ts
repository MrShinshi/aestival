/**
 * Password credential management.
 *
 * Provides bcrypt-based password hashing, verification, and the
 * register/login/set-password operations that back the credential
 * authentication flow.  All hash operations are async; the SQLite
 * wrappers are synchronous because better-sqlite3 is synchronous.
 *
 * This module is imported by auth.ts (routes) and accounts.ts
 * (unlinkAccount guard).  It does NOT depend on either of those
 * modules — no circular dependency risk.
 */

import bcrypt from 'bcryptjs';
import crypto from 'crypto';
import { getAuthDb } from './db';
import type { User } from './accounts';

// ── Constants ────────────────────────────────────────────────────────────────

const BCRYPT_ROUNDS = 12;

/** Allowed username characters: ASCII alphanumeric, underscore, hyphen, CJK. */
const USERNAME_RE = /^[\w一-鿿㐀-䶿-]{2,32}$/;

const MIN_PASSWORD_LEN = 8;
const MAX_PASSWORD_LEN = 128;

// ── Helpers ──────────────────────────────────────────────────────────────────

function uuid(): string {
  return crypto.randomUUID();
}

function rowToUser(row: any): User {
  return {
    id: row.id,
    username: row.username,
    avatar_url: row.avatar_url,
    created_at: row.created_at,
  };
}

// ── Public: hashing ──────────────────────────────────────────────────────────

/** Hash a plaintext password.  Always use 12 rounds. */
export async function hashPassword(password: string): Promise<string> {
  return bcrypt.hash(password, BCRYPT_ROUNDS);
}

/** Compare a plaintext password against a stored hash. */
export async function verifyPassword(password: string, hash: string): Promise<boolean> {
  return bcrypt.compare(password, hash);
}

// ── Public: validation ───────────────────────────────────────────────────────

/**
 * Validate a username.  Returns an error message on failure, null on success.
 */
export function validateUsername(username: unknown): string | null {
  if (!username || typeof username !== 'string') {
    return '用户名不能为空';
  }
  const trimmed = username.trim();
  if (trimmed.length < 2 || trimmed.length > 32) {
    return '用户名长度需在 2-32 个字符之间';
  }
  if (!USERNAME_RE.test(trimmed)) {
    return '用户名只能包含字母、数字、下划线、连字符和中文';
  }
  return null;
}

/**
 * Validate a password.  Returns an error message on failure, null on success.
 */
export function validatePassword(password: unknown): string | null {
  if (!password || typeof password !== 'string') {
    return '密码不能为空';
  }
  if (password.length < MIN_PASSWORD_LEN) {
    return `密码长度至少为 ${MIN_PASSWORD_LEN} 个字符`;
  }
  if (password.length > MAX_PASSWORD_LEN) {
    return `密码长度不能超过 ${MAX_PASSWORD_LEN} 个字符`;
  }
  return null;
}

// ── Public: credential operations ────────────────────────────────────────────

/**
 * Register a new user with a password.
 *
 * Creates both the `users` row and the `credentials` row in a single
 * transaction.  Throws with a descriptive message on duplicate username.
 */
export async function registerWithPassword(
  username: string,
  password: string,
): Promise<User> {
  const db = getAuthDb();
  const trimmed = username.trim();
  const hash = await hashPassword(password);
  const userId = uuid();

  const create = db.transaction(() => {
    // Check for duplicate username first (to give a friendlier error)
    const existing = db
      .prepare('SELECT id FROM users WHERE username = ?')
      .get(trimmed) as { id: string } | undefined;
    if (existing) {
      throw new Error('用户名已被占用');
    }

    db.prepare(
      'INSERT INTO users (id, username, avatar_url) VALUES (?, ?, ?)',
    ).run(userId, trimmed, '');

    db.prepare(
      'INSERT INTO credentials (user_id, password_hash) VALUES (?, ?)',
    ).run(userId, hash);
  });

  create();

  return getUserByUsername(trimmed)!;
}

/**
 * Attempt to log in with a username and password.
 *
 * Returns the User on success, null on failure.  Does NOT distinguish
 * between "wrong username" and "wrong password" — prevents enumeration.
 */
export async function loginWithPassword(
  username: string,
  password: string,
): Promise<User | null> {
  const db = getAuthDb();

  const row = db
    .prepare(
      `SELECT u.id, u.username, u.avatar_url, u.created_at, c.password_hash
       FROM users u
       JOIN credentials c ON c.user_id = u.id
       WHERE u.username = ?`,
    )
    .get(username.trim()) as any;

  if (!row) return null;

  const valid = await verifyPassword(password, row.password_hash);
  if (!valid) return null;

  return {
    id: row.id,
    username: row.username,
    avatar_url: row.avatar_url,
    created_at: row.created_at,
  };
}

/** Look up a user by exact username (case-sensitive). */
export function getUserByUsername(username: string): User | null {
  const db = getAuthDb();
  const row = db
    .prepare('SELECT * FROM users WHERE username = ?')
    .get(username) as any;
  return row ? rowToUser(row) : null;
}

/**
 * One-shot admin credential setup.
 *
 * Looks up an existing user by username, hashes the password, and stores
 * it via upsert.  Fails silently if the user doesn't exist — this is for
 * boot-time preset, not interactive registration.
 *
 * Returns true if the password was set, false if the user wasn't found.
 */
export async function ensureAdminPassword(
  username: string,
  password: string,
): Promise<boolean> {
  const user = getUserByUsername(username);
  if (!user) return false;

  const hash = await hashPassword(password);
  setPassword(user.id, hash);
  return true;
}

/**
 * Set (or update) a password for an existing user.
 *
 * Uses INSERT … ON CONFLICT DO UPDATE (upsert) so it works both for
 * OAuth users adding their first password and for credential users
 * changing their existing password.
 */
export function setPassword(userId: string, passwordHash: string): void {
  const db = getAuthDb();
  db.prepare(
    `INSERT INTO credentials (user_id, password_hash, updated_at)
     VALUES (?, ?, datetime('now'))
     ON CONFLICT(user_id) DO UPDATE SET
       password_hash = excluded.password_hash,
       updated_at = excluded.updated_at`,
  ).run(userId, passwordHash);
}

/**
 * Check whether a user has a password credential.
 */
export function hasPassword(userId: string): boolean {
  const db = getAuthDb();
  const row = db
    .prepare('SELECT 1 FROM credentials WHERE user_id = ?')
    .get(userId);
  return !!row;
}
