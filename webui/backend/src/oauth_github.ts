/**
 * GitHub OAuth 2.0 routes — PKCE flow.
 *
 * Our server cannot reach GitHub (GFW), so the browser handles the
 * authorisation-code → access_token exchange via PKCE (RFC 7636).
 * The browser then POSTs the verified user identity to /exchange.
 *
 * Flow:
 *   Frontend:  generate code_verifier → code_challenge (SHA256)
 *              redirect browser to GitHub /authorize with challenge + state
 *   Backend:   GET  /api/ui/auth/github/config  → {clientId, redirectUri, state}
 *              GET  /api/ui/auth/github/callback → 302 to /auth/callback?code=&state=
 *   Frontend:  POST code + verifier → GitHub → access_token → /user
 *              POST /api/ui/auth/github/exchange {providerUserId, username, avatarUrl, state}
 *   Backend:   create/find user → set session cookie
 */

import { Express, Request, Response } from 'express';
import { config } from './config';
import { generateState, verifyState } from './oauth';
import { findOrCreateUser, OAuthProfile } from './accounts';
import { signSession, setAuthCookie } from './auth';

export function setupGithubAuth(app: Express): void {
  /**
   * GET /api/ui/auth/github/config
   *
   * Returns the OAuth configuration the frontend needs to build the
   * GitHub authorise URL.  Generates a CSRF state token.
   *
   * Query: ?redirect=/settings&mode=link
   */
  app.get('/api/ui/auth/github/config', (req: Request, res: Response) => {
    if (!config.githubClientId) {
      res.status(500).json({ error: 'GitHub OAuth is not configured' });
      return;
    }

    const redirect = (req.query.redirect as string) || '/';
    const mode = (req.query.mode as string) === 'link' ? 'link' : 'login';
    const state = generateState({ provider: 'github', redirect, mode });

    res.json({
      clientId: config.githubClientId,
      redirectUri: config.githubRedirectUri,
      state,
    });
  });

  /**
   * GET /api/ui/auth/github/callback
   *
   * GitHub redirects the browser here with ?code=&state=.
   * We 302 the browser to the frontend with the same params — no
   * server-side HTTP calls to GitHub (the server can't egress).
   */
  app.get('/api/ui/auth/github/callback', (req: Request, res: Response) => {
    const { code, state } = req.query;

    if (!code || !state) {
      res.redirect('/auth/callback?error=no_code&provider=github');
      return;
    }

    const params = new URLSearchParams({
      code: String(code),
      state: String(state),
    });
    res.redirect(`/auth/callback?${params.toString()}`);
  });

  /**
   * POST /api/ui/auth/github/exchange
   *
   * The browser has verified the user identity with GitHub (via PKCE +
   * browser-side API calls).  It POSTs the verified identity here.
   *
   * Body: { providerUserId, username, avatarUrl, state }
   *
   * Security note: this endpoint trusts the browser-provided identity
   * because the server cannot independently verify it with GitHub (GFW).
   * The state token provides CSRF protection and carries link-mode context.
   */
  app.post('/api/ui/auth/github/exchange', (req: Request, res: Response) => {
    const { providerUserId, username, avatarUrl, state } = req.body || {};

    if (!providerUserId || !username || !state) {
      res.status(400).json({ error: 'providerUserId, username, and state are required' });
      return;
    }

    const stateData = verifyState(state);
    if (!stateData) {
      res.status(400).json({ error: 'invalid or expired state token' });
      return;
    }

    try {
      const profile: OAuthProfile = {
        provider: 'github',
        providerUserId: String(providerUserId),
        providerUsername: String(username),
        avatarUrl: String(avatarUrl || ''),
        accessToken: '', // not available in PKCE browser-side flow
      };

      const result = findOrCreateUser(profile, stateData.mode, stateData.userId);

      // Conflict — the OAuth account belongs to another user
      if (result.conflict) {
        res.json({
          conflict: true,
          targetUserId: result.user.id,
          targetUsername: result.user.username,
          sourceUserId: result.conflict.existingUser.id,
          sourceUsername: result.conflict.existingUser.username,
        });
        return;
      }

      const sessionToken = signSession(result.user);
      setAuthCookie(res, sessionToken);

      res.json({
        success: true,
        user: result.user,
        isNew: result.isNew,
        redirect: stateData.redirect || '/',
      });
    } catch (err: any) {
      console.error('[github] exchange error:', err);
      res.status(500).json({ error: 'internal server error' });
    }
  });
}
