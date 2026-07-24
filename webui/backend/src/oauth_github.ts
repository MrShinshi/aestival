/**
 * GitHub OAuth 2.0 routes.
 *
 * Flow:
 *   1. Frontend requests /config  → gets clientId + state token
 *   2. Browser redirects to GitHub /authorize
 *   3. GitHub redirects back to /callback → we 302 to frontend with ?code=&state=
 *   4. Frontend POSTs {code, state, code_verifier} to /token
 *   5. Server exchanges code with GitHub, fetches user profile, creates session
 *
 * Unlike the previous PKCE-only flow, the server now handles the GitHub API
 * calls.  The server CAN reach GitHub — our earlier connectivity issue was
 * transient GFW behaviour, not a permanent block.  This is more reliable than
 * having every browser try to reach GitHub directly.
 */

import { Express, Request, Response } from 'express';
import { config } from './config';
import { generateState, verifyState } from './oauth';
import { findOrCreateUser, OAuthProfile } from './accounts';
import { signSession, setAuthCookie } from './auth';

/** Call github.com — server can egress; browsers behind GFW often can't. */
async function githubApi(
  path: string,
  opts: { method?: string; token?: string; body?: object },
): Promise<any> {
  const headers: Record<string, string> = {
    Accept: 'application/json',
  };
  if (opts.token) {
    headers.Authorization = `Bearer ${opts.token}`;
  }
  if (opts.body) {
    headers['Content-Type'] = 'application/json';
  }

  const resp = await fetch(`https://github.com/${path}`, {
    method: opts.method || 'GET',
    headers,
    body: opts.body ? JSON.stringify(opts.body) : undefined,
  });

  const data: any = await resp.json();
  if (!resp.ok) {
    const err: any = new Error(data.error_description || data.error || resp.statusText);
    err.status = resp.status;
    throw err;
  }
  return data;
}

export function setupGithubAuth(app: Express): void {
  /**
   * GET /api/ui/auth/github/config
   *
   * Returns the OAuth configuration the frontend needs to build the
   * GitHub authorise URL.  Generates a CSRF state token.
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
   * We forward the params to the frontend via 302.
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
   * GET /api/ui/auth/github
   *
   * Login entry point.  Redirects the browser to GitHub's authorise page.
   * Query: ?mode=login|link&redirect=/some/path
   */
  app.get('/api/ui/auth/github', (req: Request, res: Response) => {
    if (!config.githubClientId) {
      res.status(500).json({ error: 'GitHub OAuth is not configured' });
      return;
    }

    const redirect = (req.query.redirect as string) || '/';
    const mode = (req.query.mode as string) === 'link' ? 'link' : 'login';
    const state = generateState({ provider: 'github', redirect, mode });

    const params = new URLSearchParams({
      client_id: config.githubClientId,
      redirect_uri: config.githubRedirectUri,
      scope: 'read:user',
      state,
    });

    res.redirect(`https://github.com/login/oauth/authorize?${params.toString()}`);
  });

  /**
   * POST /api/ui/auth/github/token
   *
   * Server-side code exchange.  The browser cannot reliably reach
   * github.com/api.github.com (GFW), so we do it here.
   *
   * Body: { code, state, code_verifier? }
   *
   * 1. Exchange code for access_token (POST github.com/login/oauth/access_token)
   * 2. Fetch user profile (GET api.github.com/user)
   * 3. Create/find user → set session cookie
   */
  app.post('/api/ui/auth/github/token', async (req: Request, res: Response) => {
    const { code, state, code_verifier } = req.body || {};

    if (!code || !state) {
      res.status(400).json({ error: 'code and state are required' });
      return;
    }

    const stateData = verifyState(state);
    if (!stateData) {
      res.status(400).json({ error: 'invalid or expired state token' });
      return;
    }

    try {
      // 1. Exchange code for access_token
      const tokenParams: Record<string, string> = {
        client_id: config.githubClientId,
        client_secret: config.githubClientSecret,
        code: String(code),
        redirect_uri: config.githubRedirectUri,
      };
      if (code_verifier) {
        tokenParams.code_verifier = String(code_verifier);
      }

      const tokenData = await githubApi('login/oauth/access_token', {
        method: 'POST',
        body: tokenParams,
      });

      const accessToken = tokenData.access_token;
      if (!accessToken) {
        console.error('[github] token exchange returned no access_token:', tokenData);
        res.status(502).json({ error: 'github returned no access_token' });
        return;
      }

      // 2. Fetch user profile
      const userData = await githubApi('api.github.com/user', {
        token: accessToken,
      });

      // 3. Create/find local user
      const profile: OAuthProfile = {
        provider: 'github',
        providerUserId: String(userData.id),
        providerUsername: userData.login,
        avatarUrl: userData.avatar_url || '',
        accessToken,
      };

      const result = findOrCreateUser(profile, stateData.mode, stateData.userId);

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
      console.error('[github] token error:', err.message || err);
      res.status(502).json({ error: err.message || 'github api error' });
    }
  });
}
