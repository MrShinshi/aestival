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
import { setupAuth, requireAuth } from './auth';
import { setupGithubAuth } from './oauth_github';
import { setupQQAuth } from './oauth_qq';
import { setupProxy } from './proxy';
import { setupLogs } from './logs';
import { setupConversations } from './conversations';
import { config } from './config';
import { ensureAdminPassword } from './credentials';

const app = express();

// Trust Nginx reverse proxy
app.set('trust proxy', 1);

app.use(cors({
  origin: config.corsOrigin,
  credentials: true,
}));

app.use(express.json({ limit: '1mb' }));
app.use(cookieParser());

// ── Public routes (no auth) ────────────────────────────────────────────────
setupAuth(app);          // /me, /logout, /merge, /unlink
setupGithubAuth(app);    // /auth/github, /auth/github/callback
setupQQAuth(app);        // /auth/qq, /auth/qq/callback

// Health check
app.get('/api/ui/health', (_req, res) => {
  res.json({ status: 'ok', timestamp: new Date().toISOString() });
});

// ── Protected routes (JWT session cookie required) ─────────────────────────
app.use('/api/ui/agents', requireAuth);
app.use('/api/ui/conversations', requireAuth);
app.use('/api/ui/logs', requireAuth);
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

app.listen(config.port, async () => {
  console.log(`aestival Web UI backend listening on http://localhost:${config.port}`);

  // ── Admin credential preset ─────────────────────────────────────────────
  // If AUTH_ADMIN_USER + AUTH_ADMIN_PASS are set, bind a password to the
  // named OAuth user on startup so they can also log in with credentials.
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
