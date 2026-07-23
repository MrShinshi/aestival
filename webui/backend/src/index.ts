/**
 * aestival Web UI Backend
 *
 * Express server that:
 * 1. Auto-login (no OAuth required)
 * 2. Issues JWT tokens for the bot's internal API
 * 3. Proxies management requests to the bot's internal API (127.0.0.1:9090)
 * 4. Reads bot log files and SQLite databases directly (read-only)
 * 5. Serves frontend static files
 */

import express from 'express';
import cors from 'cors';
import helmet from 'helmet';
import path from 'path';
import { setupAuth, requireAuth } from './auth';
import { setupProxy } from './proxy';
import { setupLogs } from './logs';
import { setupConversations } from './conversations';

const app = express();
const PORT = parseInt(process.env.PORT || '3000', 10);

// Trust Nginx reverse proxy
app.set('trust proxy', 1);

// Security headers
app.use(helmet({
  contentSecurityPolicy: false, // handled by frontend meta tags
}));

app.use(cors({
  origin: process.env.CORS_ORIGIN || 'http://localhost:5173',
  credentials: true,
}));

app.use(express.json({ limit: '1mb' }));

// Public routes (no auth)
setupAuth(app);

// Health check
app.get('/api/ui/health', (_req, res) => {
  res.json({ status: 'ok', timestamp: new Date().toISOString() });
});

// ── Protected routes (JWT required) ─────────────────────────────────────
app.use('/api/ui/agents', requireAuth);
app.use('/api/ui/conversations', requireAuth);
app.use('/api/ui/logs', requireAuth);
app.use('/api/ui/status', requireAuth);

setupProxy(app);
setupLogs(app);
setupConversations(app);

// Serve frontend static files in production
const frontendDist = path.resolve(__dirname, '../../frontend/dist');
app.use(express.static(frontendDist));

// SPA fallback — serve index.html for any unmatched route
app.get('*', (_req, res) => {
  res.sendFile(path.join(frontendDist, 'index.html'));
});

app.listen(PORT, () => {
  console.log(`aestival Web UI backend listening on http://localhost:${PORT}`);
});
