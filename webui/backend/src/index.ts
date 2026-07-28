/**
 * aestival Web UI Backend
 *
 * Express server that:
 * 1. Handles OAuth login (GitHub + QQ)
 * 2. Issues session JWTs via httpOnly cookies
 * 3. Proxies management requests to the bot's internal API (127.0.0.1:9090)
 * 4. Reads bot log files and SQLite databases directly (read-only)
 * 5. Serves frontend static files
 */

// Must be the FIRST import — loads .env before other modules read process.env
import './config';

import express from 'express';
import cors from 'cors';
import cookieParser from 'cookie-parser';
import path from 'path';
import http from 'http';
import { setupAuth, requireAuth, requireAdmin } from './auth';
import { setupGithubAuth } from './oauth_github';
import { setupQQAuth } from './oauth_qq';
import { setupProxy } from './proxy';
import { setupLogs } from './logs';
import { setupConversations } from './conversations';
import { config } from './config';
import { ensureAdminPassword } from './credentials';
import { closeAuthDb } from './db';

const app = express();

// Trust Nginx reverse proxy
app.set('trust proxy', 1);

app.use(cors({
  origin: config.corsOrigin,
  credentials: true,
}));

app.use(express.json({ limit: '1mb' }));
app.use(cookieParser());

// ── Rate limiting for auth endpoints ──────────────────────────────────────
// In-memory sliding-window limiter — avoids adding express-rate-limit
// dependency for this small deployment.

function rateLimit(maxRequests: number, windowMs: number) {
  const hits = new Map<string, { count: number; resetAt: number }>();

  // Purge stale entries every 60s
  setInterval(() => {
    const now = Date.now();
    for (const [key, entry] of hits) {
      if (now > entry.resetAt) hits.delete(key);
    }
  }, 60_000).unref();

  return (req: express.Request, res: express.Response, next: express.NextFunction) => {
    const key = req.ip || req.socket.remoteAddress || 'unknown';
    const now = Date.now();
    let entry = hits.get(key);
    if (!entry || now > entry.resetAt) {
      entry = { count: 0, resetAt: now + windowMs };
      hits.set(key, entry);
    }
    entry.count++;
    if (entry.count > maxRequests) {
      res.status(429).json({ error: '请求过于频繁，请稍后重试' });
      return;
    }
    next();
  };
}

// ── Public routes (no auth) ────────────────────────────────────────────────
setupAuth(app);          // /me, /logout, /merge, /unlink
setupGithubAuth(app);    // /auth/github, /auth/github/callback
setupQQAuth(app);        // /auth/qq, /auth/qq/callback

// Rate-limit auth endpoints
app.use('/api/ui/auth/login', rateLimit(30, 15 * 60 * 1000));
app.use('/api/ui/auth/register', rateLimit(10, 15 * 60 * 1000));

// Health check — verifies downstream bot API reachability
app.get('/api/ui/health', async (_req, res) => {
  const checks: Record<string, string> = {};
  try {
    await new Promise<void>((resolve, reject) => {
      const req = http.get(config.botApiUrl + '/health', (resp) => {
        checks.botApi = resp.statusCode === 200 ? 'ok' : `status ${resp.statusCode}`;
        resp.resume();
        resolve();
      });
      req.on('error', (e) => { checks.botApi = `unreachable: ${e.message}`; resolve(); });
      req.setTimeout(3000, () => { req.destroy(); checks.botApi = 'timeout'; resolve(); });
    });
  } catch {
    checks.botApi = 'error';
  }
  res.json({ status: 'ok', checks, timestamp: new Date().toISOString() });
});

// ── Protected routes (JWT session cookie required) ─────────────────────────
//
// Administration endpoints — login required AND admin username.
// Regular users are 403'd from agents, conversations, and logs.
app.use('/api/ui/agents', requireAuth, requireAdmin);
app.use('/api/ui/conversations', requireAuth, requireAdmin);
app.use('/api/ui/logs', requireAuth, requireAdmin);
app.use('/api/ui/metrics', requireAuth, requireAdmin);
app.use('/api/ui/tokens', requireAuth, requireAdmin);

// Status is read-only health info — any authenticated user can see it.
app.use('/api/ui/status', requireAuth);

setupProxy(app);
setupLogs(app);
setupConversations(app);

// ── Serve frontend static files in production ──────────────────────────────
const frontendDist = path.resolve(__dirname, '../../frontend/dist');
app.use(express.static(frontendDist));

// SPA fallback — serve index.html for any unmatched route
app.get('*', (_req, res) => {
  res.sendFile(path.join(frontendDist, 'index.html'));
});

// ── Global error handler (must be LAST middleware) ────────────────────────
app.use((err: Error, _req: express.Request, res: express.Response, _next: express.NextFunction) => {
  console.error('[unhandled]', err.message || err);
  res.status(500).json({ error: 'Internal server error' });
});

app.listen(config.port, async () => {
  console.log(`aestival Web UI backend listening on http://localhost:${config.port}`);

  // ── Admin credential preset ─────────────────────────────────────────────
  if (config.adminUser && config.adminPass) {
    const ok = await ensureAdminPassword(config.adminUser, config.adminPass);
    if (ok) {
      console.log(`  Admin password: set for "${config.adminUser}"`);
    } else {
      console.log(`  Admin password: user "${config.adminUser}" not found (yet)`);
    }
  }
  if (config.githubClientId) {
    console.log('  GitHub OAuth: enabled');
  } else {
    console.log('  GitHub OAuth: not configured (set GITHUB_CLIENT_ID)');
  }
  if (config.qqAppId) {
    console.log('  QQ OAuth: enabled');
  } else {
    console.log('  QQ OAuth: not configured (set QQ_APP_ID)');
  }
});

// ── Graceful shutdown ─────────────────────────────────────────────────────
function shutdown(signal: string) {
  console.log(`\n[webui] ${signal} received — shutting down...`);
  try { closeAuthDb(); console.log('[webui] auth DB closed'); } catch {}
  process.exit(0);
}
process.on('SIGTERM', () => shutdown('SIGTERM'));
process.on('SIGINT', () => shutdown('SIGINT'));
