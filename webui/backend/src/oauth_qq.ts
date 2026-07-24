/**
 * QQ互联 (QQ Connect) OAuth 2.0 routes.
 *
 * QQ's OAuth implementation has three notable differences from GitHub:
 *   1. The token endpoint returns URL-encoded key-value pairs, not JSON.
 *   2. Getting the openid requires a separate /oauth2.0/me call.
 *   3. The /oauth2.0/me response is wrapped in JSONP: callback({...});
 *
 * Flow:
 *   GET /api/ui/auth/qq           → redirect to QQ authorize page
 *   GET /api/ui/auth/qq/callback  → QQ redirects here with ?code=
 */

import { Express, Request, Response } from 'express';
import jwt from 'jsonwebtoken';
import { config } from './config';
import { generateState, verifyState } from './oauth';
import { findOrCreateUser, OAuthProfile } from './accounts';
import { signSession, setAuthCookie } from './auth';

const QQ_AUTHORIZE = 'https://graph.qq.com/oauth2.0/authorize';
const QQ_TOKEN = 'https://graph.qq.com/oauth2.0/token';
const QQ_OPENID = 'https://graph.qq.com/oauth2.0/me';
const QQ_USER_INFO = 'https://graph.qq.com/user/get_user_info';

/**
 * QQ's /oauth2.0/me endpoint returns JSONP:
 *   callback( {"client_id":"...","openid":"..."} );
 *
 * Strip the wrapper and return the parsed object.
 */
function parseJsonp(body: string): any {
  const start = body.indexOf('(');
  const end = body.lastIndexOf(')');
  if (start === -1 || end === -1) {
    throw new Error('Invalid JSONP response from QQ');
  }
  const json = body.slice(start + 1, end);
  return JSON.parse(json);
}

export function setupQQAuth(app: Express): void {
  /**
   * GET /api/ui/auth/qq
   *
   * Initiate QQ OAuth.  Supports ?redirect= and ?mode=link query params.
   */
  app.get('/api/ui/auth/qq', (req: Request, res: Response) => {
    if (!config.qqAppId) {
      res.status(500).json({ error: 'QQ OAuth is not configured' });
      return;
    }

    const redirect = (req.query.redirect as string) || '/';
    const mode = (req.query.mode as string) === 'link' ? 'link' : 'login';
    const stateParams: Parameters<typeof generateState>[0] = {
      provider: 'qq',
      redirect,
      mode,
    };

    if (mode === 'link') {
      const token = req.cookies?.auth_token;
      if (token) {
        try {
          const payload = jwt.verify(token, config.jwtSecret, {
            algorithms: ['HS256'],
          }) as any;
          stateParams.userId = payload.sub;
        } catch {
          stateParams.mode = 'login';
        }
      } else {
        stateParams.mode = 'login';
      }
    }

    const state = generateState(stateParams);

    const params = new URLSearchParams({
      response_type: 'code',
      client_id: config.qqAppId,
      redirect_uri: config.qqRedirectUri,
      scope: 'get_user_info',
      state,
    });

    res.redirect(`${QQ_AUTHORIZE}?${params.toString()}`);
  });

  /**
   * GET /api/ui/auth/qq/callback
   *
   * QQ redirects the browser here after the user authorises.
   * PUBLIC endpoint — state token proves legitimacy.
   */
  app.get('/api/ui/auth/qq/callback', async (req: Request, res: Response) => {
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

    if (!code || typeof code !== 'string') {
      res.redirect(
        `/auth/callback?error=no_code&provider=qq`,
      );
      return;
    }

    try {
      // 2. Exchange code for access token
      // QQ returns URL-encoded key-value pairs, NOT JSON.
      const tokenParams = new URLSearchParams({
        grant_type: 'authorization_code',
        client_id: config.qqAppId,
        client_secret: config.qqAppSecret,
        code,
        redirect_uri: config.qqRedirectUri,
        fmt: 'json',
      });

      const tokenResp = await fetch(`${QQ_TOKEN}?${tokenParams.toString()}`);

      if (!tokenResp.ok) {
        console.error('[qq] token exchange failed:', tokenResp.status);
        res.redirect(`/auth/callback?error=token_exchange&provider=qq`);
        return;
      }

      // Try parsing as JSON first (when fmt=json), fall back to URL-encoded
      const tokenText = await tokenResp.text();
      let accessToken: string;
      let refreshToken: string | undefined;
      let expiresIn: string | undefined;

      try {
        const json = JSON.parse(tokenText);
        if (json.error) {
          console.error('[qq] token error:', json.error, json.error_description);
          res.redirect(`/auth/callback?error=token_exchange&provider=qq`);
          return;
        }
        accessToken = json.access_token;
        refreshToken = json.refresh_token;
        expiresIn = json.expires_in;
      } catch {
        // Fallback: URL-encoded format
        const parsed = new URLSearchParams(tokenText);
        accessToken = parsed.get('access_token') || '';
        refreshToken = parsed.get('refresh_token') || undefined;
        expiresIn = parsed.get('expires_in') || undefined;
      }

      if (!accessToken) {
        console.error('[qq] no access_token in response:', tokenText);
        res.redirect(`/auth/callback?error=token_exchange&provider=qq`);
        return;
      }

      // 3. Get openid (QQ requires a separate call)
      const openidResp = await fetch(
        `${QQ_OPENID}?access_token=${encodeURIComponent(accessToken)}&fmt=json`,
      );

      if (!openidResp.ok) {
        console.error('[qq] openid fetch failed:', openidResp.status);
        res.redirect(`/auth/callback?error=user_fetch&provider=qq`);
        return;
      }

      const openidText = await openidResp.text();
      let openid: string;

      try {
        // Try JSON first
        const openidJson = JSON.parse(openidText);
        openid = openidJson.openid;
      } catch {
        // Parse JSONP: callback({...});
        const openidData = parseJsonp(openidText);
        openid = openidData.openid;
      }

      if (!openid) {
        console.error('[qq] no openid in response:', openidText);
        res.redirect(`/auth/callback?error=user_fetch&provider=qq`);
        return;
      }

      // 4. Get user info
      const userInfoParams = new URLSearchParams({
        access_token: accessToken,
        oauth_consumer_key: config.qqAppId,
        openid,
      });

      const userResp = await fetch(`${QQ_USER_INFO}?${userInfoParams.toString()}`);

      if (!userResp.ok) {
        console.error('[qq] user info fetch failed:', userResp.status);
        res.redirect(`/auth/callback?error=user_fetch&provider=qq`);
        return;
      }

      const userData = await userResp.json() as {
        ret: number;
        msg: string;
        nickname: string;
        figureurl_qq_1: string;
        figureurl_qq_2: string;
      };
      if (userData.ret !== 0) {
        console.error('[qq] user info error:', userData.ret, userData.msg);
        res.redirect(`/auth/callback?error=user_fetch&provider=qq`);
        return;
      }

      // QQ avatar: prefer figureurl_qq_2 (100×100)
      const avatarUrl = userData.figureurl_qq_2 || userData.figureurl_qq_1 || '';

      // 5. Find or create account
      const profile: OAuthProfile = {
        provider: 'qq',
        providerUserId: openid,
        providerUsername: userData.nickname || '',
        avatarUrl,
        accessToken,
        refreshToken,
        tokenExpiresAt: expiresIn
          ? new Date(Date.now() + parseInt(expiresIn, 10) * 1000).toISOString()
          : undefined,
      };

      const result = findOrCreateUser(profile, stateData.mode, stateData.userId);

      // 6. Handle conflict
      if (result.conflict) {
        const params = new URLSearchParams({
          error: 'conflict',
          provider: 'qq',
          targetUserId: result.user.id,
          targetUsername: result.user.username,
          sourceUserId: result.conflict.existingUser.id,
          sourceUsername: result.conflict.existingUser.username,
        });
        res.redirect(`/auth/callback?${params.toString()}`);
        return;
      }

      // 7. Success
      const sessionToken = signSession(result.user);
      setAuthCookie(res, sessionToken);

      const redirect = stateData.redirect || '/';
      res.redirect(
        `/auth/callback?login=success&redirect=${encodeURIComponent(redirect)}`,
      );
    } catch (err: any) {
      console.error('[qq] unexpected error:', err);
      res.redirect(`/auth/callback?error=server_error&provider=qq`);
    }
  });
}
