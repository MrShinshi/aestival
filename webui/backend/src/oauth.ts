/**
 * OAuth state token helpers.
 *
 * The OAuth "state" parameter prevents CSRF by carrying a signed JWT through
 * the redirect flow.  The state token is short-lived (10 minutes) and embeds
 * the provider name, return URL, operation mode, and a random nonce for
 * replay protection.
 *
 * Anti-replay: each nonce can only be consumed once.  A LRU-style Map tracks
 * recently consumed nonces with a TTL matching the state token lifetime.
 *
 * The same JWT_SECRET used for session tokens signs the state token.
 * This is a stateless alternative to a server-side session store.
 */

import jwt from 'jsonwebtoken';
import crypto from 'crypto';
import { config } from './config';

export interface OAuthState {
  provider: 'github' | 'qq';
  redirect: string;        // where to send the browser after auth (frontend path)
  mode: 'login' | 'link';  // login = new session; link = add platform to existing user
  userId?: string;         // present only in 'link' mode — the authenticated user
  nonce: string;           // random bytes, prevents replay
}

const STATE_SECRET = config.jwtSecret;
const STATE_EXPIRY = '10m';

/** In-memory consumed-nonce set.  Entries auto-expire after 10 minutes. */
const consumedNonces = new Map<string, number>(); // nonce → expiry timestamp (ms)

/** Purge expired nonces.  Called before each verification. */
function purgeExpiredNonces() {
  const now = Date.now();
  for (const [nonce, expires] of consumedNonces) {
    if (now > expires) consumedNonces.delete(nonce);
  }
}

/**
 * Generate a signed state token for an OAuth redirect.
 * The returned string is placed in the OAuth authorize URL's `state` parameter.
 */
export function generateState(params: Omit<OAuthState, 'nonce'>): string {
  const state: OAuthState = {
    ...params,
    nonce: crypto.randomBytes(16).toString('hex'),
  };
  return jwt.sign(state, STATE_SECRET, {
    algorithm: 'HS256',
    expiresIn: STATE_EXPIRY,
  });
}

/**
 * Verify and decode an OAuth state token received from a callback.
 * Each token can be consumed at most once — subsequent attempts return null.
 * Returns null on any failure (expired, tampered, malformed, or replayed).
 */
export function verifyState(token: string): OAuthState | null {
  let payload: OAuthState;
  try {
    payload = jwt.verify(token, STATE_SECRET, { algorithms: ['HS256'] }) as OAuthState;
  } catch {
    return null;
  }

  purgeExpiredNonces();

  // Check replay: this nonce must not have been used before.
  if (consumedNonces.has(payload.nonce)) {
    console.warn('[oauth] replayed state token detected — nonce already consumed');
    return null;
  }

  // Mark nonce as consumed; auto-expire after 10 minutes.
  consumedNonces.set(payload.nonce, Date.now() + 10 * 60 * 1000);

  return payload;
}
