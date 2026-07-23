/**
 * Proxy requests to the bot's internal management API (127.0.0.1:9090).
 *
 * The Web UI backend adds the JWT Bearer token to every proxied request.
 * The bot's management_api verifies this token.
 */

import { Express, Request, Response } from 'express';
import jwt from 'jsonwebtoken';

const BOT_API = process.env.BOT_API_URL || 'http://127.0.0.1:9090';
const JWT_SECRET = process.env.JWT_SECRET || '';
const PROXY_TIMEOUT_MS = 15_000;

function botToken(): string {
  return jwt.sign(
    { sub: 'admin', iat: Math.floor(Date.now() / 1000) },
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

  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), PROXY_TIMEOUT_MS);

  const opts: RequestInit = { method, headers, signal: controller.signal };
  if (body && method !== 'GET') {
    opts.body = JSON.stringify(body);
  }

  try {
    const resp = await fetch(url, opts);
    clearTimeout(timer);
    const data = await resp.json();
    return { status: resp.status, data };
  } finally {
    clearTimeout(timer);
  }
}

export function setupProxy(app: Express) {
  // ── Agents ──────────────────────────────────────────────────────────
  app.get('/api/ui/agents', async (_req, res) => {
    try {
      const r = await proxyToBot('GET', '/api/v1/agents', null, botToken());
      res.status(r.status).json(r.data);
    } catch (err: any) {
      res.status(502).json({ error: 'bot API unreachable', detail: err.message });
    }
  });

  app.post('/api/ui/agents', async (req, res) => {
    try {
      const r = await proxyToBot('POST', '/api/v1/agents', req.body, botToken());
      res.status(r.status).json(r.data);
    } catch (err: any) {
      res.status(502).json({ error: 'bot API unreachable', detail: err.message });
    }
  });

  app.delete('/api/ui/agents/:id', async (req, res) => {
    try {
      const r = await proxyToBot('DELETE', `/api/v1/agents/${req.params.id}`, null, botToken());
      res.status(r.status).json(r.data);
    } catch (err: any) {
      res.status(502).json({ error: 'bot API unreachable', detail: err.message });
    }
  });

  app.post('/api/ui/agents/:id/:action', async (req, res) => {
    const { id, action } = req.params;
    if (action !== 'start' && action !== 'stop') {
      res.status(400).json({ error: 'invalid action' });
      return;
    }
    try {
      const r = await proxyToBot('POST', `/api/v1/agents/${id}/${action}`, null, botToken());
      res.status(r.status).json(r.data);
    } catch (err: any) {
      res.status(502).json({ error: 'bot API unreachable', detail: err.message });
    }
  });

  app.put('/api/ui/agents/:id/config', async (req, res) => {
    try {
      const r = await proxyToBot('PUT', `/api/v1/agents/${req.params.id}/config`, req.body, botToken());
      res.status(r.status).json(r.data);
    } catch (err: any) {
      res.status(502).json({ error: 'bot API unreachable', detail: err.message });
    }
  });

  // ── Status ──────────────────────────────────────────────────────────
  app.get('/api/ui/status', async (_req, res) => {
    try {
      const r = await proxyToBot('GET', '/api/v1/health', null, '');
      res.status(r.status).json(r.data);
    } catch (err: any) {
      res.status(502).json({ error: 'bot API unreachable', detail: err.message });
    }
  });
}
