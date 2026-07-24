/**
 * Centralised environment configuration.
 *
 * Import this module BEFORE any other local module — it calls dotenv.config()
 * so that process.env is populated before other modules read it at load time.
 *
 * All other modules should import { config } from './config' instead of
 * reading process.env directly.
 */

import dotenv from 'dotenv';
import path from 'path';

// Resolve .env relative to the backend directory (one level above src/).
// In dev (tsx) __dirname is src/; in prod (node dist/) it's dist/.
// Walk up until we find .env or stop at the project root.
function findEnv(): string | undefined {
  const candidates = [
    path.resolve(__dirname, '..', '.env'),
    path.resolve(__dirname, '..', '..', '.env'),
    path.resolve(process.cwd(), '.env'),
    path.resolve(process.cwd(), 'webui', 'backend', '.env'),
    path.resolve(process.cwd(), 'backend', '.env'),
  ];
  for (const c of candidates) {
    try {
      const fs = require('fs');
      if (fs.existsSync(c)) return c;
    } catch {}
  }
  return undefined;
}

const envPath = findEnv();
if (envPath) {
  dotenv.config({ path: envPath });
} else {
  dotenv.config(); // fallback to default behaviour
}

// Refuse to start without a configured secret
if (!process.env.JWT_SECRET) {
  console.error('FATAL: JWT_SECRET environment variable is not set. Refusing to start.');
  process.exit(1);
}

export const config = {
  jwtSecret: process.env.JWT_SECRET || '',
  port: parseInt(process.env.PORT || '3000', 10),
  corsOrigin: process.env.CORS_ORIGIN || 'http://localhost:5173',
  frontendUrl: process.env.FRONTEND_URL || 'http://localhost:5173',
  nodeEnv: process.env.NODE_ENV || 'development',

  // Bot API (for proxying management requests)
  botApiUrl: process.env.BOT_API_URL || 'http://127.0.0.1:9090',
  botLogPath: process.env.BOT_LOG_PATH || '',
  botLogBase: process.env.BOT_LOG_BASE || '',
  botContextsBase: process.env.BOT_CONTEXTS_BASE || '',

  // Auth SQLite database
  authDbPath: process.env.AUTH_DB_PATH || path.resolve(__dirname, '..', 'data', 'auth.db'),

  // GitHub OAuth
  githubClientId: process.env.GITHUB_CLIENT_ID || '',
  githubClientSecret: process.env.GITHUB_CLIENT_SECRET || '',
  githubRedirectUri: process.env.GITHUB_REDIRECT_URI || '',

  // QQ OAuth (QQ互联)
  qqAppId: process.env.QQ_APP_ID || '',
  qqAppSecret: process.env.QQ_APP_SECRET || '',
  qqRedirectUri: process.env.QQ_REDIRECT_URI || '',
};
