/**
 * Proxy requests to the bot's internal management API (127.0.0.1:9090).
 *
 * The Web UI backend adds the JWT Bearer token to every proxied request.
 * The bot's management_api verifies this token.
 */

import { Express, Request, Response, NextFunction } from 'express';
import { requireAuth } from './auth';
import jwt from 'jsonwebtoken';

const BOT_API = process.env.BOT_API_URL || 'http://127.0.0.1:9090';
const JWT_SECRET = process.env.JWT_SECRET || '';

// Generate a JWT for the bot API
function botToken(req: Request): string {
  const user = req.session.githubUser || 'unknown';
  return jwt.sign(
    { sub: user, iat: Math.floor(Date.now() / 1000) },
    JWT_SECRET,
    { expiresIn: '1h', algorithm: 'HS256' }
  );
}

async function proxyToBot(method: string, path: string, body: unknown, token: string) {
  const url = `${BOT_API}${path}`;
  const headers: Record<string, string> = {
    'Content-Type': 'application/json',
    'Authorization': `Bearer ${token}`,
  };

  const opts: RequestInit = { method, headers };
  if (body && method !== 'GET') {
    opts.body = JSON.stringify(body);
  }

  const resp = await fetch(url, opts);
  const data = await resp.json();
  return { status: resp.status, data };
}

export function setupProxy(app: Express) {
  // ── Agents ──────────────────────────────────────────────────────────
  app.get('/api/ui/agents', requireAuth, async (req, res) => {
    try {
      const r = await proxyToBot('GET', '/api/v1/agents', null, botToken(req));
      res.status(r.status).json(r.data);
    } catch (err: any) {
      res.status(502).json({ error: 'bot API unreachable', detail: err.message });
    }
  });

  app.post('/api/ui/agents', requireAuth, async (req, res) => {
    try {
      const r = await proxyToBot('POST', '/api/v1/agents', req.body, botToken(req));
      res.status(r.status).json(r.data);
    } catch (err: any) {
      res.status(502).json({ error: 'bot API unreachable', detail: err.message });
    }
  });

  app.delete('/api/ui/agents/:id', requireAuth, async (req, res) => {
    try {
      const r = await proxyToBot('DELETE', `/api/v1/agents/${req.params.id}`, null, botToken(req));
      res.status(r.status).json(r.data);
    } catch (err: any) {
      res.status(502).json({ error: 'bot API unreachable', detail: err.message });
    }
  });

  app.post('/api/ui/agents/:id/:action', requireAuth, async (req, res) => {
    const { id, action } = req.params;
    if (action !== 'start' && action !== 'stop') {
      res.status(400).json({ error: 'invalid action' });
      return;
    }
    try {
      const r = await proxyToBot('POST', `/api/v1/agents/${id}/${action}`, null, botToken(req));
      res.status(r.status).json(r.data);
    } catch (err: any) {
      res.status(502).json({ error: 'bot API unreachable', detail: err.message });
    }
  });

  app.put('/api/ui/agents/:id/config', requireAuth, async (req, res) => {
    try {
      const r = await proxyToBot('PUT', `/api/v1/agents/${req.params.id}/config`, req.body, botToken(req));
      res.status(r.status).json(r.data);
    } catch (err: any) {
      res.status(502).json({ error: 'bot API unreachable', detail: err.message });
    }
  });

  // ── Health (no auth required) ───────────────────────────────────────
  app.get('/api/ui/status', requireAuth, async (_req, res) => {
    try {
      const r = await proxyToBot('GET', '/api/v1/health', null, '');
      res.status(r.status).json(r.data);
    } catch (err: any) {
      res.status(502).json({ error: 'bot API unreachable', detail: err.message });
    }
  });
}
