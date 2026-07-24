/**
 * GitHub OAuth 2.0 routes.
 *
 * Flow:
 *   GET /api/ui/auth/github           → redirect to GitHub authorize page
 *   GET /api/ui/auth/github/callback  → GitHub redirects here with ?code=
 *
 * The callback exchanges the code for an access token (server-side only —
 * the client_secret never reaches the browser), fetches the user profile,
 * and establishes a session via httpOnly cookie.
 *
 * Query params on the initial request:
 *   ?redirect=/settings   where to send the browser after login (default: /)
 *   ?mode=link            add this platform to the current user (requires auth)
 */

import { Express, Request, Response } from 'express';
import jwt from 'jsonwebtoken';
import { config } from './config';
import { generateState, verifyState } from './oauth';
import { findOrCreateUser, OAuthProfile } from './accounts';
import { signSession, setAuthCookie } from './auth';

const GITHUB_AUTHORIZE = 'https://github.com/login/oauth/authorize';
const GITHUB_TOKEN = 'https://github.com/login/oauth/access_token';
const GITHUB_USER = 'https://api.github.com/user';

export function setupGithubAuth(app: Express): void {
  /**
   * GET /api/ui/auth/github
   *
   * Initiate GitHub OAuth.  Supports ?redirect= and ?mode=link query params.
   */
  app.get('/api/ui/auth/github', (req: Request, res: Response) => {
    if (!config.githubClientId) {
      res.status(500).json({ error: 'GitHub OAuth is not configured' });
      return;
    }

    const redirect = (req.query.redirect as string) || '/';
    const mode = (req.query.mode as string) === 'link' ? 'link' : 'login';
    const stateParams: Parameters<typeof generateState>[0] = {
      provider: 'github',
      redirect,
      mode,
    };

    if (mode === 'link') {
      // Read current user from cookie (lightweight — no full requireAuth needed)
      const token = req.cookies?.auth_token;
      if (token) {
        try {
          const payload = jwt.verify(token, config.jwtSecret, { algorithms: ['HS256'] }) as any;
          stateParams.userId = payload.sub;
        } catch {
          // If the session is invalid, fall back to login mode
          stateParams.mode = 'login';
        }
      } else {
        stateParams.mode = 'login';
      }
    }

    const state = generateState(stateParams);

    const params = new URLSearchParams({
      client_id: config.githubClientId,
      redirect_uri: config.githubRedirectUri,
      scope: 'read:user',
      state,
    });

    res.redirect(`${GITHUB_AUTHORIZE}?${params.toString()}`);
  });

  /**
   * GET /api/ui/auth/github/callback
   *
   * GitHub redirects the browser here after the user authorises.
   * This is a PUBLIC endpoint (no auth guard) — the state token is the
   * only proof that the request is legitimate.
   */
  app.get('/api/ui/auth/github/callback', async (req: Request, res: Response) => {
    const { code, state } = req.query;

    // 1. Verify state token
    if (!state || typeof state !== 'string') {
      res.redirect(`/auth/callback?error=invalid_state`);
      return;
    }

    const stateData = verifyState(state);
    if (!stateData) {
      res.redirect(`/auth/callback?error=invalid_state`);
      return;
    }

    // 2. Verify code is present
    if (!code || typeof code !== 'string') {
      res.redirect(
        `/auth/callback?error=no_code&provider=github`,
      );
      return;
    }

    try {
      // 3. Exchange code for access token (server-side — secret never leaves here)
      const tokenResp = await fetch(GITHUB_TOKEN, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          Accept: 'application/json',
        },
        body: JSON.stringify({
          client_id: config.githubClientId,
          client_secret: config.githubClientSecret,
          code,
          redirect_uri: config.githubRedirectUri,
        }),
      });

      if (!tokenResp.ok) {
        const errBody = await tokenResp.text();
        console.error('[github] token exchange failed:', tokenResp.status, errBody);
        res.redirect(`/auth/callback?error=token_exchange&provider=github`);
        return;
      }

      const tokenData = await tokenResp.json() as { access_token: string };
      const accessToken = tokenData.access_token;
      if (!accessToken) {
        console.error('[github] no access_token in response:', JSON.stringify(tokenData));
        res.redirect(`/auth/callback?error=token_exchange&provider=github`);
        return;
      }

      // 4. Fetch user profile
      const userResp = await fetch(GITHUB_USER, {
        headers: {
          Authorization: `Bearer ${accessToken}`,
          Accept: 'application/json',
          'User-Agent': 'aestival',
        },
      });

      if (!userResp.ok) {
        console.error('[github] user fetch failed:', userResp.status);
        res.redirect(`/auth/callback?error=user_fetch&provider=github`);
        return;
      }

      const userData = await userResp.json() as { id: number; login: string; avatar_url: string };

      // 5. Find or create account
      const profile: OAuthProfile = {
        provider: 'github',
        providerUserId: String(userData.id),
        providerUsername: userData.login,
        avatarUrl: userData.avatar_url || '',
        accessToken,
      };

      const result = findOrCreateUser(profile, stateData.mode, stateData.userId);

      // 6. Handle conflict
      if (result.conflict) {
        // Redirect with enough info for the frontend to show a merge dialog
        const params = new URLSearchParams({
          error: 'conflict',
          provider: 'github',
          targetUserId: result.user.id,
          targetUsername: result.user.username,
          sourceUserId: result.conflict.existingUser.id,
          sourceUsername: result.conflict.existingUser.username,
        });
        res.redirect(`/auth/callback?${params.toString()}`);
        return;
      }

      // 7. Success — set session cookie and redirect
      const sessionToken = signSession(result.user);
      setAuthCookie(res, sessionToken);

      const redirect = stateData.redirect || '/';
      res.redirect(
        `/auth/callback?login=success&redirect=${encodeURIComponent(redirect)}`,
      );
    } catch (err: any) {
      console.error('[github] unexpected error:', err);
      res.redirect(`/auth/callback?error=server_error&provider=github`);
    }
  });
}
