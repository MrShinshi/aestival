/**
 * Proxy requests to the bot's internal management API (127.0.0.1:9090).
 *
 * The Web UI backend adds a JWT Bearer token signed with the authenticated
 * user's identity.  The bot's management_api verifies this token via the
 * shared jwt_secret.
 *
 * JWT tokens are cached per-user by the auth module to avoid re-signing
 * on every proxied request.
 */

import { Express, Request, Response } from 'express';
import { config } from './config';
import { getBotToken } from './auth';
import { sanitizeAgentId } from './sanitize';

const PROXY_TIMEOUT_MS = 15_000;

// ── Proxy helper ───────────────────────────────────────────────────────────

async function proxyToBot(
  method: string,
  path: string,
  body: unknown,
  token: string,
) {
  const url = `${config.botApiUrl}${path}`;
  const headers: Record<string, string> = {
    'Content-Type': 'application/json',
  };
  if (token) {
    headers['Authorization'] = `Bearer ${token}`;
  }

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

/**
 * Get a bot API token for the authenticated user.
 * Falls back to 'system' when there's no authenticated user context.
 */
function userToken(req: Request): string {
  const user = (req as any).user;
  return getBotToken(user?.username || 'system');
}

// ── Route setup ────────────────────────────────────────────────────────────

export function setupProxy(app: Express) {
  // ── Agents ────────────────────────────────────────────────────────────
  app.get('/api/ui/agents', async (req, res) => {
    try {
      const r = await proxyToBot('GET', '/api/v1/agents', null, userToken(req));
      res.status(r.status).json(r.data);
    } catch (err: any) {
      internalError(res, err, 'GET /agents');
    }
  });

  app.post('/api/ui/agents', async (req, res) => {
    try {
      const r = await proxyToBot(
        'POST',
        '/api/v1/agents',
        req.body,
        userToken(req),
      );
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
      const r = await proxyToBot(
        'DELETE',
        `/api/v1/agents/${encodeURIComponent(id)}`,
        null,
        userToken(req),
      );
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
      const r = await proxyToBot(
        'POST',
        `/api/v1/agents/${encodeURIComponent(id)}/${encodeURIComponent(action)}`,
        null,
        userToken(req),
      );
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
      const r = await proxyToBot(
        'PUT',
        `/api/v1/agents/${encodeURIComponent(id)}/config`,
        req.body,
        userToken(req),
      );
      res.status(r.status).json(r.data);
    } catch (err: any) {
      internalError(res, err, `PUT /agents/${id}/config`);
    }
  });

  // ── Status ────────────────────────────────────────────────────────────
  // The bot's /health endpoint is public — no token needed.
  app.get('/api/ui/status', async (_req, res) => {
    try {
      const r = await proxyToBot('GET', '/api/v1/health', null, '');
      res.status(r.status).json(r.data);
    } catch (err: any) {
      internalError(res, err, 'GET /status');
    }
  });
}
