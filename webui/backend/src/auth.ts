/**
 * Authentication module.
 *
 * Session model: JWT (HS256, 24h) stored in an httpOnly cookie.
 * The browser never touches the token; it is automatically sent with
 * every request via `credentials: 'include'`.
 *
 * The Express → C++ channel still uses `Authorization: Bearer` —
 * the proxy reads the cookie, extracts the user, and signs a separate
 * short-lived JWT that the C++ management API verifies.
 */

import { Express, Request, Response, NextFunction } from 'express';
import jwt from 'jsonwebtoken';
import { config } from './config';
import { getUserById, getLinkedAccounts, mergeAccounts, unlinkAccount } from './accounts';

// ── Types ──────────────────────────────────────────────────────────────────

export interface SessionUser {
  sub: string;       // user UUID
  username: string;
  avatar_url: string;
  iat: number;
  exp: number;
}

const SESSION_EXPIRY = '24h';
const SESSION_MAX_AGE_MS = 24 * 60 * 60 * 1000;
const COOKIE_NAME = 'auth_token';

// ── Bot API token cache ────────────────────────────────────────────────────

const botTokenCache = new Map<string, { token: string; expiresAt: number }>();

/**
 * Return a JWT for authenticating with the C++ management API.
 * Cached per-username; tokens live 1 hour and are reused until
 * 5 minutes before expiry.
 */
export function getBotToken(username: string): string {
  const now = Math.floor(Date.now() / 1000);
  const cached = botTokenCache.get(username);
  if (cached && now < cached.expiresAt - 300) {
    return cached.token;
  }

  const token = jwt.sign(
    { sub: username, iat: now },
    config.jwtSecret,
    { expiresIn: '1h', algorithm: 'HS256' },
  );
  botTokenCache.set(username, { token, expiresAt: now + 3600 });
  return token;
}

// ── Session helpers ────────────────────────────────────────────────────────

/** Sign a session JWT for a user. */
export function signSession(user: { id: string; username: string; avatar_url: string }): string {
  return jwt.sign(
    {
      sub: user.id,
      username: user.username,
      avatar_url: user.avatar_url,
    },
    config.jwtSecret,
    { algorithm: 'HS256', expiresIn: SESSION_EXPIRY },
  );
}

/** Set the auth cookie on a response. */
export function setAuthCookie(res: Response, token: string): void {
  res.cookie(COOKIE_NAME, token, {
    httpOnly: true,
    secure: config.nodeEnv === 'production',
    sameSite: 'strict',
    path: '/',
    maxAge: SESSION_MAX_AGE_MS,
  });
}

/** Clear the auth cookie. */
export function clearAuthCookie(res: Response): void {
  res.clearCookie(COOKIE_NAME, { path: '/' });
}

// ── Middleware ─────────────────────────────────────────────────────────────

/**
 * Verify the session cookie and attach user info to `req.user`.
 * Returns 401 when the cookie is missing, invalid, or expired.
 */
export function requireAuth(req: Request, res: Response, next: NextFunction): void {
  const token = req.cookies?.[COOKIE_NAME];
  if (!token) {
    res.status(401).json({ error: 'authentication required' });
    return;
  }

  try {
    const payload = jwt.verify(token, config.jwtSecret, {
      algorithms: ['HS256'],
    }) as SessionUser;
    (req as any).user = payload;
    next();
  } catch {
    clearAuthCookie(res);
    res.status(401).json({ error: 'invalid or expired session' });
  }
}

// ── Route setup ────────────────────────────────────────────────────────────

export function setupAuth(app: Express): void {
  // ── Public (no auth guard) ────────────────────────────────────────────

  /**
   * GET /api/ui/auth/me
   *
   * Query the current session state.  This is intentionally NOT behind
   * requireAuth — it returns {authenticated: false} gracefully so the
   * frontend can determine auth state on initial load without triggering
   * a 401 error.
   */
  app.get('/api/ui/auth/me', (req: Request, res: Response) => {
    const token = req.cookies?.[COOKIE_NAME];
    if (!token) {
      res.json({ authenticated: false });
      return;
    }

    try {
      const payload = jwt.verify(token, config.jwtSecret, {
        algorithms: ['HS256'],
      }) as SessionUser;

      const linkedAccounts = getLinkedAccounts(payload.sub);

      res.json({
        authenticated: true,
        user: {
          id: payload.sub,
          username: payload.username,
          avatar_url: payload.avatar_url,
        },
        linked_accounts: linkedAccounts.map((a) => ({
          id: a.id,
          provider: a.provider,
          provider_username: a.provider_username,
          created_at: a.created_at,
        })),
      });
    } catch {
      res.json({ authenticated: false });
    }
  });

  /**
   * GET /api/ui/auth/logout
   *
   * Clear the session cookie and redirect to the frontend login page.
   */
  app.get('/api/ui/auth/logout', (_req: Request, res: Response) => {
    clearAuthCookie(res);
    res.redirect('/login');
  });

  // ── Protected routes ──────────────────────────────────────────────────

  /**
   * POST /api/ui/auth/merge
   *
   * Merge two user accounts.  The authenticated user must be the target.
   * Body: { targetUserId: string, sourceUserId: string }
   */
  app.post('/api/ui/auth/merge', requireAuth, (req: Request, res: Response) => {
    const user = (req as any).user as SessionUser;
    const { targetUserId, sourceUserId } = req.body || {};

    if (!targetUserId || !sourceUserId) {
      res.status(400).json({ error: 'targetUserId and sourceUserId are required' });
      return;
    }

    // Only allow merging INTO the currently authenticated user's account
    if (targetUserId !== user.sub) {
      res.status(403).json({ error: 'can only merge into your own account' });
      return;
    }

    try {
      const merged = mergeAccounts(targetUserId, sourceUserId);
      // Re-sign the session with updated user info
      const newToken = signSession(merged);
      setAuthCookie(res, newToken);
      res.json({ merged: true, user: merged });
    } catch (err: any) {
      res.status(400).json({ error: err.message || 'merge failed' });
    }
  });

  /**
   * POST /api/ui/auth/unlink
   *
   * Remove an OAuth provider link from the current user.
   * Body: { provider: 'github' | 'qq' }
   */
  app.post('/api/ui/auth/unlink', requireAuth, (req: Request, res: Response) => {
    const user = (req as any).user as SessionUser;
    const { provider } = req.body || {};

    if (!provider || !['github', 'qq'].includes(provider)) {
      res.status(400).json({ error: 'valid provider is required' });
      return;
    }

    try {
      unlinkAccount(user.sub, provider);
      // Re-sign session with fresh data
      const updatedUser = getUserById(user.sub);
      if (updatedUser) {
        const newToken = signSession(updatedUser);
        setAuthCookie(res, newToken);
      }
      res.json({ unlinked: true, provider });
    } catch (err: any) {
      res.status(400).json({ error: err.message || 'unlink failed' });
    }
  });
}
