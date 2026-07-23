/**
 * Proxy requests to the bot's internal management API (127.0.0.1:9090).
 *
 * The Web UI backend adds the JWT Bearer token to every proxied request.
 * The bot's management_api verifies this token.
 */

import { Express, Request, Response } from 'express';
import jwt from 'jsonwebtoken';
import { sanitizeAgentId } from './sanitize';

const BOT_API = process.env.BOT_API_URL || 'http://127.0.0.1:9090';
const JWT_SECRET = process.env.JWT_SECRET || '';
const PROXY_TIMEOUT_MS = 15_000;

// ── JWT cache (avoid re-signing for every proxied request) ────────────────
let cachedToken: string | null = null;
let cachedTokenExpiry: number = 0;

function botToken(): string {
  const now = Math.floor(Date.now() / 1000);
  // Reuse token if it has at least 5 minutes of remaining validity
  if (cachedToken && now < cachedTokenExpiry - 300) {
    return cachedToken;
  }
  cachedToken = jwt.sign(
    { sub: 'admin', iat: now },
    JWT_SECRET,
    { expiresIn: '1h', algorithm: 'HS256' }
  );
  cachedTokenExpiry = now + 3600;
  return cachedToken;
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

function internalError(res: Response, err: any, detail: string) {
  console.error(`[proxy] ${detail}:`, err);
  res.status(502).json({ error: 'bot API unreachable' });
}

export function setupProxy(app: Express) {
  // ── Agents ──────────────────────────────────────────────────────────
  app.get('/api/ui/agents', async (_req, res) => {
    try {
      const r = await proxyToBot('GET', '/api/v1/agents', null, botToken());
      res.status(r.status).json(r.data);
    } catch (err: any) {
      internalError(res, err, 'GET /agents');
    }
  });

  app.post('/api/ui/agents', async (req, res) => {
    try {
      const r = await proxyToBot('POST', '/api/v1/agents', req.body, botToken());
      res.status(r.status).json(r.data);
    } catch (err: any) {
      internalError(res, err, 'POST /agents');
    }
  });

  app.delete('/api/ui/agents/:id', async (req, res) => {
    const id = sanitizeAgentId(req.params.id);
    if (id === '_invalid_') {
      res.status(400).json({ error: 'invalid agent id' });
      return;
    }
    try {
      const r = await proxyToBot('DELETE', `/api/v1/agents/${encodeURIComponent(id)}`, null, botToken());
      res.status(r.status).json(r.data);
    } catch (err: any) {
      internalError(res, err, `DELETE /agents/${id}`);
    }
  });

  app.post('/api/ui/agents/:id/:action', async (req, res) => {
    const { id: rawId, action } = req.params;
    if (action !== 'start' && action !== 'stop') {
      res.status(400).json({ error: 'invalid action' });
      return;
    }
    const id = sanitizeAgentId(rawId);
    if (id === '_invalid_') {
      res.status(400).json({ error: 'invalid agent id' });
      return;
    }
    try {
      const r = await proxyToBot('POST', `/api/v1/agents/${encodeURIComponent(id)}/${encodeURIComponent(action)}`, null, botToken());
      res.status(r.status).json(r.data);
    } catch (err: any) {
      internalError(res, err, `POST /agents/${id}/${action}`);
    }
  });

  app.put('/api/ui/agents/:id/config', async (req, res) => {
    const id = sanitizeAgentId(req.params.id);
    if (id === '_invalid_') {
      res.status(400).json({ error: 'invalid agent id' });
      return;
    }
    try {
      const r = await proxyToBot('PUT', `/api/v1/agents/${encodeURIComponent(id)}/config`, req.body, botToken());
      res.status(r.status).json(r.data);
    } catch (err: any) {
      internalError(res, err, `PUT /agents/${id}/config`);
    }
  });

  // ── Status ──────────────────────────────────────────────────────────
  app.get('/api/ui/status', async (_req, res) => {
    try {
      const r = await proxyToBot('GET', '/api/v1/health', null, '');
      res.status(r.status).json(r.data);
    } catch (err: any) {
      internalError(res, err, 'GET /status');
    }
  });
}
