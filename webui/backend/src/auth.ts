/**
 * Auto-login — no OAuth required.
 *
 * Requires environment variable:
 *   JWT_SECRET  (shared with bot management_api.jwt_secret)
 */

import { Express } from 'express';
import jwt from 'jsonwebtoken';

const JWT_SECRET = process.env.JWT_SECRET || '';

export function setupAuth(app: Express) {
  // Always return success — no login required
  app.get('/api/ui/auth/me', (_req, res) => {
    res.json({ user: 'admin' });
  });

  // Issue JWT for the bot management API
  app.get('/api/ui/auth/token', (_req, res) => {
    if (!JWT_SECRET) {
      res.status(500).json({ error: 'JWT_SECRET not configured' });
      return;
    }
    const token = jwt.sign(
      { sub: 'admin', iat: Math.floor(Date.now() / 1000) },
      JWT_SECRET,
      { expiresIn: '24h', algorithm: 'HS256' }
    );
    res.json({ token, user: 'admin', expires_in: 86400 });
  });
}
