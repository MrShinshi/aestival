/**
 * GitHub OAuth + JWT authentication.
 *
 * Requires environment variables:
 *   GITHUB_CLIENT_ID
 *   GITHUB_CLIENT_SECRET
 *   GITHUB_CALLBACK_URL  (e.g. https://your-server.com/api/ui/auth/github/callback)
 *   ALLOWED_USERS        (comma-separated GitHub usernames)
 *   JWT_SECRET           (shared with bot management_api.jwt_secret)
 */

import { Express, Request, Response, NextFunction } from 'express';
import passport from 'passport';
import { Strategy as GitHubStrategy } from 'passport-github2';
import jwt from 'jsonwebtoken';

const JWT_SECRET = process.env.JWT_SECRET || '';
const ALLOWED_USERS = (process.env.ALLOWED_USERS || '').split(',').map(s => s.trim()).filter(Boolean);

// Extend express-session types
declare module 'express-session' {
  interface SessionData {
    githubUser?: string;
  }
}

export function setupAuth(app: Express) {
  if (!process.env.GITHUB_CLIENT_ID || !process.env.GITHUB_CLIENT_SECRET) {
    console.warn('[auth] GitHub OAuth not configured — login will fail');
    return;
  }

  passport.use(new GitHubStrategy({
    clientID: process.env.GITHUB_CLIENT_ID,
    clientSecret: process.env.GITHUB_CLIENT_SECRET,
    callbackURL: process.env.GITHUB_CALLBACK_URL || 'http://localhost:3000/api/ui/auth/github/callback',
  }, (accessToken, _refreshToken, profile, done) => {
    const username = profile.username || '';
    if (ALLOWED_USERS.length > 0 && !ALLOWED_USERS.includes(username)) {
      return done(new Error(`User '${username}' is not in the allowed list`));
    }
    return done(null, { username });
  }));

  app.use(passport.initialize());
  app.use(passport.session());

  passport.serializeUser((user: any, done) => done(null, user));
  passport.deserializeUser((user: any, done) => done(null, user));

  // Login endpoint
  app.get('/api/ui/auth/github', passport.authenticate('github', { scope: ['user:email'] }));

  // Callback
  app.get('/api/ui/auth/github/callback',
    passport.authenticate('github', { failureRedirect: '/login?error=oauth_failed' }),
    (req, res) => {
      req.session.githubUser = (req.user as any)?.username;
      res.redirect('/');
    });

  // Issue JWT for the frontend
  app.get('/api/ui/auth/token', (req, res) => {
    const user = req.session.githubUser;
    if (!user) {
      res.status(401).json({ error: 'not authenticated' });
      return;
    }

    if (!JWT_SECRET) {
      res.status(500).json({ error: 'JWT_SECRET not configured' });
      return;
    }

    const token = jwt.sign(
      { sub: user, iat: Math.floor(Date.now() / 1000) },
      JWT_SECRET,
      { expiresIn: '24h', algorithm: 'HS256' }
    );

    res.json({ token, user, expires_in: 86400 });
  });

  // Who am I?
  app.get('/api/ui/auth/me', (req, res) => {
    if (!req.session.githubUser) {
      res.status(401).json({ error: 'not authenticated' });
      return;
    }
    res.json({ user: req.session.githubUser });
  });

  // Logout
  app.post('/api/ui/auth/logout', (req, res) => {
    req.session.destroy(() => {
      res.json({ status: 'ok' });
    });
  });
}

// Middleware: require GitHub session
export function requireAuth(req: Request, res: Response, next: NextFunction) {
  if (!req.session.githubUser) {
    res.status(401).json({ error: 'not authenticated' });
    return;
  }
  next();
}
