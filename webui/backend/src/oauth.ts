/**
 * OAuth state token helpers.
 *
 * The OAuth "state" parameter prevents CSRF by carrying a signed JWT through
 * the redirect flow.  The state token is short-lived (10 minutes) and embeds
 * the provider name, return URL, operation mode, and a random nonce for
 * replay protection.
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
 * Returns null on any failure (expired, tampered, malformed).
 */
export function verifyState(token: string): OAuthState | null {
  try {
    const payload = jwt.verify(token, STATE_SECRET, { algorithms: ['HS256'] });
    return payload as OAuthState;
  } catch {
    return null;
  }
}
