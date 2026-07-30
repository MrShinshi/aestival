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

// ── Metrics history ring buffer ──────────────────────────────────────────────
//
// Caches recent health snapshots so the frontend can back-fill the resource
// chart on page load instead of starting with an empty graph.
//
// A background interval polls the bot every 1 s to keep the buffer warm even
// when no browser is open.  Every /api/ui/status request also feeds the
// buffer (opportunistic).

const HISTORY_MAX = 180;  // 3 minutes at 1-second poll rate (safe margin above 2 min)
const HISTORY_WINDOW_MS = 180_000;
const historyBuffer: Array<{ ts: number; data: unknown }> = [];

function feedHistory(data: unknown): void {
  const now = Date.now();
  historyBuffer.push({ ts: now, data });
  // Trim to window — keep entries within last 3 minutes so the
  // frontend always gets a full 2-minute chart on page load, even
  // with slight timing jitter between background poller ticks.
  const cutoff = now - HISTORY_WINDOW_MS;
  while (historyBuffer.length > 0 && historyBuffer[0].ts < cutoff) {
    historyBuffer.shift();
  }
  // Hard cap as safety valve
  while (historyBuffer.length > HISTORY_MAX) {
    historyBuffer.shift();
  }
}

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

  const opts: RequestInit = { method, headers };
  if (body && method !== 'GET') {
    opts.body = JSON.stringify(body);
  }

  // Try once, then retry once after 500ms for transient failures
  let lastError: any;
  for (let attempt = 0; attempt < 2; attempt++) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), PROXY_TIMEOUT_MS);
    opts.signal = controller.signal;

    try {
      const resp = await fetch(url, opts);
      clearTimeout(timer);
      const data = await resp.json();
      return { status: resp.status, data };
    } catch (err: any) {
      clearTimeout(timer);
      lastError = err;
      if (attempt === 0) {
        await new Promise(r => setTimeout(r, 500));
      }
    }
  }

  throw lastError;
}

function internalError(res: Response, err: any, detail: string) {
  console.error(`[proxy] ${detail}:`, err);
  const msg = err?.cause?.code === 'ECONNREFUSED'
    ? 'Bot 进程未运行，请在服务器上检查 bot 服务状态'
    : 'Bot API 无响应，请稍后重试';
  res.status(502).json({ error: msg });
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
      feedHistory(r.data);  // feed the metrics ring buffer
      res.status(r.status).json(r.data);
    } catch (err: any) {
      internalError(res, err, 'GET /status');
    }
  });

  // ── Metrics history ────────────────────────────────────────────────────
  // Returns cached health snapshots from the ring buffer so the frontend
  // resource chart is immediately populated on page load.
  // Each snapshot is annotated with _ts (server epoch ms) for time alignment.
  app.get('/api/ui/metrics/history', async (_req, res) => {
    try {
      const snapshots = historyBuffer.map(e => ({ ...(e.data as any), _ts: e.ts }));
      res.json({ snapshots });
    } catch (err: any) {
      internalError(res, err, 'GET /metrics/history');
    }
  });

  // ── Metrics ──────────────────────────────────────────────────────────
  app.get('/api/ui/metrics', async (req, res) => {
    try {
      const r = await proxyToBot('GET', '/api/v1/metrics', null, userToken(req));
      res.status(r.status).json(r.data);
    } catch (err: any) {
      internalError(res, err, 'GET /metrics');
    }
  });

  // ── Per-agent metrics ─────────────────────────────────────────────────
  app.get('/api/ui/agents/:id/metrics', async (req, res) => {
    const id = sanitizeAgentId(req.params.id);
    if (id === '_invalid_') {
      res.status(400).json({ error: 'invalid agent id' });
      return;
    }
    try {
      const r = await proxyToBot(
        'GET',
        `/api/v1/agents/${encodeURIComponent(id)}/metrics`,
        null,
        userToken(req),
      );
      res.status(r.status).json(r.data);
    } catch (err: any) {
      internalError(res, err, `GET /agents/${id}/metrics`);
    }
  });

  // ── Token stats ───────────────────────────────────────────────────────
  app.get('/api/ui/tokens', async (req, res) => {
    try {
      const r = await proxyToBot('GET', '/api/v1/tokens/stats', null, userToken(req));
      // C++ json_response wraps non-object bodies in {"status":"ok","data":...}
      res.status(r.status).json((r.data as any)?.data || r.data);
    } catch (err: any) {
      internalError(res, err, 'GET /tokens');
    }
  });

  // ── Background metrics collector ───────────────────────────────────────
  // Polls the bot health endpoint every second to keep the ring buffer warm
  // so that when a user opens the dashboard the past-2-min resource chart
  // is immediately populated.
  const bgPoll = setInterval(async () => {
    try {
      const r = await proxyToBot('GET', '/api/v1/health', null, '');
      feedHistory(r.data);
    } catch {
      // silent — bot may be temporarily unreachable
    }
  }, 1000);
  bgPoll.unref();
}
