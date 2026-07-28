/**
 * Shared OAuth callback handler.
 *
 * Both GitHub and QQ OAuth flows need the same post-profile logic:
 *   findOrCreateUser → conflict? → sign session + set cookie → respond.
 * This module extracts that common tail so each provider only handles
 * the provider-specific API dance.
 */

import { Request, Response } from 'express';
import { OAuthProfile, findOrCreateUser } from './accounts';
import { signSession, setAuthCookie } from './auth';
import type { OAuthState } from './oauth';

/**
 * Complete the OAuth flow after the provider profile has been fetched.
 *
 * Handles three outcomes:
 *   1. Conflict — a different user already owns this OAuth identity.
 *      Responds with conflict details so the frontend can offer a merge UI.
 *   2. New user created — responds with isNew = true.
 *   3. Existing user — responds with isNew = false.
 *
 * For callback-style providers (QQ) that do everything server-side,
 * pass `redirectOnSuccess` to 302 the browser.  For token-exchange
 * providers (GitHub) that use JSON responses, leave it undefined.
 */
export function handleOAuthProfile(
  profile: OAuthProfile,
  stateData: OAuthState,
  req: Request,
  res: Response,
  opts?: { redirectOnSuccess?: boolean },
) {
  const result = findOrCreateUser(profile, stateData.mode, stateData.userId);

  if (result.conflict) {
    if (opts?.redirectOnSuccess) {
      // QQ: redirect with conflict params for the frontend callback page
      const params = new URLSearchParams({
        error: 'conflict',
        provider: profile.provider,
        targetUserId: result.user.id,
        targetUsername: result.user.username,
        sourceUserId: result.conflict.existingUser.id,
        sourceUsername: result.conflict.existingUser.username,
      });
      res.redirect(`/auth/callback?${params.toString()}`);
    } else {
      // GitHub: JSON response
      res.json({
        conflict: true,
        targetUserId: result.user.id,
        targetUsername: result.user.username,
        sourceUserId: result.conflict.existingUser.id,
        sourceUsername: result.conflict.existingUser.username,
      });
    }
    return;
  }

  const sessionToken = signSession(result.user);
  setAuthCookie(res, sessionToken);

  if (opts?.redirectOnSuccess) {
    // QQ: 302 to frontend callback page
    const redirect = stateData.redirect || '/';
    res.redirect(`/auth/callback?login=success&redirect=${encodeURIComponent(redirect)}`);
  } else {
    // GitHub: JSON response
    res.json({
      success: true,
      user: result.user,
      isNew: result.isNew,
      redirect: stateData.redirect || '/',
    });
  }
}
