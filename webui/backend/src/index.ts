/**
 * aestival Web UI Backend
 *
 * Express server that:
 * 1. Handles GitHub OAuth login
 * 2. Issues JWT tokens for the frontend
 * 3. Proxies management requests to the bot's internal API (127.0.0.1:9090)
 * 4. Reads bot log files and SQLite databases directly (read-only)
 */

import express from 'express';
import session from 'express-session';
import cors from 'cors';
import { setupAuth } from './auth';
import { setupProxy } from './proxy';
import { setupLogs } from './logs';
import { setupConversations } from './conversations';

const app = express();
const PORT = parseInt(process.env.PORT || '3000', 10);
const SESSION_SECRET = process.env.SESSION_SECRET || 'change-me-in-production';

// Trust Nginx reverse proxy
app.set('trust proxy', 1);

app.use(cors({
  origin: process.env.CORS_ORIGIN || 'http://localhost:5173',
  credentials: true,
}));

app.use(express.json());
app.use(session({
  secret: SESSION_SECRET,
  resave: false,
  saveUninitialized: false,
  cookie: {
    secure: process.env.NODE_ENV === 'production',
    httpOnly: true,
    maxAge: 24 * 60 * 60 * 1000, // 24 hours
  },
}));

// Routes
setupAuth(app);
setupProxy(app);
setupLogs(app);
setupConversations(app);

// Health check
app.get('/api/ui/health', (_req, res) => {
  res.json({ status: 'ok', timestamp: new Date().toISOString() });
});

app.listen(PORT, () => {
  console.log(`aestival Web UI backend listening on http://localhost:${PORT}`);
});
