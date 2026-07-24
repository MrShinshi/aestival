/**
 * Account creation, lookup, linking, and merging logic.
 *
 * This module answers the core question: given an OAuth profile from a
 * third-party provider, which local user account should this resolve to?
 *
 * Lenient-mode policy: if a user logs in with a platform that has never been
 * seen before, a new account is automatically created.  The user can later
 * link additional platforms in Settings.  When a link attempt reveals that
 * the OAuth account already belongs to a different user, a conflict is
 * reported so the frontend can offer a merge UI.
 */

import crypto from 'crypto';
import { getAuthDb } from './db';

// ── Types ──────────────────────────────────────────────────────────────────

export interface User {
  id: string;
  username: string;
  avatar_url: string;
  created_at: string;
}

export interface OAuthAccount {
  id: string;
  user_id: string;
  provider: string;
  provider_user_id: string;
  provider_username: string;
  created_at: string;
}

export interface OAuthProfile {
  provider: 'github' | 'qq';
  providerUserId: string;
  providerUsername: string;
  avatarUrl: string;
  accessToken: string;
  refreshToken?: string;
  tokenExpiresAt?: string;
}

export interface FindOrCreateResult {
  user: User;
  isNew: boolean;
  conflict?: {
    existingUser: User;
    existingOAuth: OAuthAccount;
  };
}

// ── Helpers ────────────────────────────────────────────────────────────────

function uuid(): string {
  return crypto.randomUUID();
}

/** Generate a unique username, appending suffixes when the base is taken. */
function uniqueUsername(db: ReturnType<typeof getAuthDb>, base: string): string {
  // Try the base name first
  const existing = db
    .prepare('SELECT id FROM users WHERE username = ?')
    .get(base) as { id: string } | undefined;
  if (!existing) return base;

  // Try platform suffix
  const withSuffix = `${base}-`;
  let counter = 2;

  // Check {base}-{platform} first, then {base}-2, {base}-3, ...
  // We don't know the provider here so we just use numeric suffixes.
  let candidate = `${withSuffix}${counter}`;
  while (
    db.prepare('SELECT id FROM users WHERE username = ?').get(candidate)
  ) {
    counter++;
    candidate = `${withSuffix}${counter}`;
  }
  return candidate;
}

function rowToUser(row: any): User {
  return {
    id: row.id,
    username: row.username,
    avatar_url: row.avatar_url,
    created_at: row.created_at,
  };
}

function rowToOAuthAccount(row: any): OAuthAccount {
  return {
    id: row.id,
    user_id: row.user_id,
    provider: row.provider,
    provider_user_id: row.provider_user_id,
    provider_username: row.provider_username,
    created_at: row.created_at,
  };
}

// ── Public API ─────────────────────────────────────────────────────────────

/**
 * Find an existing user by OAuth profile, or create a new one.
 *
 * @param profile     OAuth profile from the provider
 * @param mode        'login' = establish a session; 'link' = add to current user
 * @param currentUserId  Required when mode='link'; the authenticated user's ID
 */
export function findOrCreateUser(
  profile: OAuthProfile,
  mode: 'login' | 'link',
  currentUserId?: string,
): FindOrCreateResult {
  const db = getAuthDb();

  // 1. Look up the OAuth account
  const oauthRow = db
    .prepare(
      `SELECT oa.*, u.username as user_username, u.avatar_url as user_avatar_url,
              u.created_at as user_created_at
       FROM oauth_accounts oa
       JOIN users u ON u.id = oa.user_id
       WHERE oa.provider = ? AND oa.provider_user_id = ?`,
    )
    .get(profile.provider, profile.providerUserId) as any;

  // 2. OAuth account exists — update tokens and return
  if (oauthRow) {
    const user: User = {
      id: oauthRow.user_id,
      username: oauthRow.user_username,
      avatar_url: oauthRow.user_avatar_url,
      created_at: oauthRow.user_created_at,
    };

    // Update stored tokens and profile info
    db.prepare(
      `UPDATE oauth_accounts
       SET access_token = ?, refresh_token = ?, token_expires_at = ?,
           provider_username = ?
       WHERE id = ?`,
    ).run(
      profile.accessToken,
      profile.refreshToken || '',
      profile.tokenExpiresAt || null,
      profile.providerUsername,
      oauthRow.id,
    );

    // Update avatar if we got a new one
    if (profile.avatarUrl && user.avatar_url !== profile.avatarUrl) {
      db.prepare('UPDATE users SET avatar_url = ? WHERE id = ?').run(
        profile.avatarUrl,
        user.id,
      );
      user.avatar_url = profile.avatarUrl;
    }

    // mode='link' conflict check: OAuth account belongs to a different user
    if (mode === 'link' && currentUserId && oauthRow.user_id !== currentUserId) {
      // Fetch the current user (the one trying to link) for the conflict UI
      const currentUser = getUserById(currentUserId);
      return {
        user: currentUser!,
        isNew: false,
        conflict: {
          existingUser: user,
          existingOAuth: rowToOAuthAccount(oauthRow),
        },
      };
    }

    return { user, isNew: false };
  }

  // 3. OAuth account does NOT exist
  if (mode === 'login') {
    // Lenient mode: create a new user automatically
    const username = uniqueUsername(db, profile.providerUsername || `${profile.provider}_user`);
    const userId = uuid();

    db.prepare(
      'INSERT INTO users (id, username, avatar_url) VALUES (?, ?, ?)',
    ).run(userId, username, profile.avatarUrl);

    db.prepare(
      `INSERT INTO oauth_accounts
         (id, user_id, provider, provider_user_id, provider_username,
          access_token, refresh_token, token_expires_at)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?)`,
    ).run(
      uuid(), userId, profile.provider, profile.providerUserId,
      profile.providerUsername, profile.accessToken,
      profile.refreshToken || '', profile.tokenExpiresAt || null,
    );

    const user: User = {
      id: userId,
      username,
      avatar_url: profile.avatarUrl,
      created_at: new Date().toISOString(),
    };
    return { user, isNew: true };
  }

  // mode='link': attach this OAuth account to the current user
  if (!currentUserId) {
    throw new Error('currentUserId is required for link mode');
  }

  db.prepare(
    `INSERT INTO oauth_accounts
       (id, user_id, provider, provider_user_id, provider_username,
        access_token, refresh_token, token_expires_at)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?)`,
  ).run(
    uuid(), currentUserId, profile.provider, profile.providerUserId,
    profile.providerUsername, profile.accessToken,
    profile.refreshToken || '', profile.tokenExpiresAt || null,
  );

  const user = getUserById(currentUserId);
  return { user: user!, isNew: false };
}

/** Get a user by ID. Returns null if not found. */
export function getUserById(userId: string): User | null {
  const db = getAuthDb();
  const row = db.prepare('SELECT * FROM users WHERE id = ?').get(userId) as any;
  return row ? rowToUser(row) : null;
}

/** List all OAuth accounts linked to a user. */
export function getLinkedAccounts(userId: string): OAuthAccount[] {
  const db = getAuthDb();
  const rows = db
    .prepare('SELECT * FROM oauth_accounts WHERE user_id = ? ORDER BY created_at')
    .all(userId) as any[];
  return rows.map(rowToOAuthAccount);
}

/**
 * Merge two user accounts.
 *
 * All OAuth accounts from `sourceUserId` are moved to `targetUserId`,
 * then `sourceUserId` is deleted.  Runs in a transaction.
 *
 * Returns the target user on success, or throws on failure.
 */
export function mergeAccounts(targetUserId: string, sourceUserId: string): User {
  if (targetUserId === sourceUserId) {
    throw new Error('Cannot merge an account into itself');
  }

  const db = getAuthDb();

  const target = getUserById(targetUserId);
  const source = getUserById(sourceUserId);
  if (!target) throw new Error('Target user not found');
  if (!source) throw new Error('Source user not found');

  const migrateAll = db.transaction(() => {
    // Check for UNIQUE constraint violations: the source might have OAuth
    // accounts whose (provider, provider_user_id) already exist on target.
    // Skip those — they're already linked.
    const sourceOAuths = db
      .prepare('SELECT * FROM oauth_accounts WHERE user_id = ?')
      .all(sourceUserId) as any[];

    const targetOAuths = db
      .prepare('SELECT provider, provider_user_id FROM oauth_accounts WHERE user_id = ?')
      .all(targetUserId) as { provider: string; provider_user_id: string }[];

    const targetKeys = new Set(
      targetOAuths.map((r) => `${r.provider}:${r.provider_user_id}`),
    );

    let moved = 0;
    for (const oa of sourceOAuths) {
      const key = `${oa.provider}:${oa.provider_user_id}`;
      if (targetKeys.has(key)) {
        // Already exists on target — delete the duplicate from source
        db.prepare('DELETE FROM oauth_accounts WHERE id = ?').run(oa.id);
      } else {
        db.prepare('UPDATE oauth_accounts SET user_id = ? WHERE id = ?').run(
          targetUserId,
          oa.id,
        );
        targetKeys.add(key);
        moved++;
      }
    }

    // Use the better avatar between the two accounts
    if (source.avatar_url && !target.avatar_url) {
      db.prepare('UPDATE users SET avatar_url = ? WHERE id = ?').run(
        source.avatar_url,
        targetUserId,
      );
    }

    // Delete the source user (cascades any remaining oauth_accounts)
    db.prepare('DELETE FROM users WHERE id = ?').run(sourceUserId);

    return moved;
  });

  migrateAll();

  // Return updated target
  return getUserById(targetUserId)!;
}

/**
 * Unlink an OAuth provider from a user.
 *
 * Refuses if this is the only linked account — every user must have at
 * least one way to log in.
 */
export function unlinkAccount(userId: string, provider: string): void {
  const db = getAuthDb();

  const linked = getLinkedAccounts(userId);
  if (linked.length <= 1) {
    throw new Error(
      'Cannot unlink the only login method. Add another platform first.',
    );
  }

  const target = linked.find((a) => a.provider === provider);
  if (!target) {
    throw new Error(`No ${provider} account linked to this user`);
  }

  db.prepare('DELETE FROM oauth_accounts WHERE id = ?').run(target.id);
}
