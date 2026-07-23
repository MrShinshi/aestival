/**
 * Auto-login — no OAuth required.
 *
 * Requires environment variable:
 *   JWT_SECRET  (shared with bot management_api.jwt_secret)
 *
 * JWT middleware validates Bearer tokens on protected routes.
 * /api/ui/auth/token and /api/ui/health remain unauthenticated.
 */

import { Express, Request, Response, NextFunction } from 'express';
import jwt from 'jsonwebtoken';

const JWT_SECRET = process.env.JWT_SECRET || '';

// Refuse to start without a configured secret.
if (!JWT_SECRET) {
  console.error('FATAL: JWT_SECRET environment variable is not set. Refusing to start.');
  process.exit(1);
}

/** JWT verification middleware — attach to protected routes. */
export function requireAuth(req: Request, res: Response, next: NextFunction) {
  const authHeader = req.headers.authorization;
  if (!authHeader || !authHeader.startsWith('Bearer ')) {
    res.status(401).json({ error: 'missing or invalid Authorization header' });
    return;
  }

  const token = authHeader.slice(7); // strip "Bearer "
  try {
    const payload = jwt.verify(token, JWT_SECRET, { algorithms: ['HS256'] });
    (req as any).user = payload;
    next();
  } catch (err: any) {
    res.status(401).json({ error: 'invalid or expired token' });
  }
}

export function setupAuth(app: Express) {
  // Always return success — no login required
  app.get('/api/ui/auth/me', (_req, res) => {
    res.json({ user: 'admin' });
  });

  // Issue JWT for the bot management API
  app.get('/api/ui/auth/token', (_req, res) => {
    const token = jwt.sign(
      { sub: 'admin', iat: Math.floor(Date.now() / 1000) },
      JWT_SECRET,
      { expiresIn: '24h', algorithm: 'HS256' }
    );
    res.json({ token, user: 'admin', expires_in: 86400 });
  });
}
