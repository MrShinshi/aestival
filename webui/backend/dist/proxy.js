"use strict";
/**
 * Proxy requests to the bot's internal management API (127.0.0.1:9090).
 *
 * The Web UI backend adds the JWT Bearer token to every proxied request.
 * The bot's management_api verifies this token.
 */
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.setupProxy = setupProxy;
const jsonwebtoken_1 = __importDefault(require("jsonwebtoken"));
const BOT_API = process.env.BOT_API_URL || 'http://127.0.0.1:9090';
const JWT_SECRET = process.env.JWT_SECRET || '';
function botToken() {
    return jsonwebtoken_1.default.sign({ sub: 'admin', iat: Math.floor(Date.now() / 1000) }, JWT_SECRET, { expiresIn: '1h', algorithm: 'HS256' });
}
async function proxyToBot(method, path, body, token) {
    const url = `${BOT_API}${path}`;
    const headers = {
        'Content-Type': 'application/json',
        'Authorization': `Bearer ${token}`,
    };
    const opts = { method, headers };
    if (body && method !== 'GET') {
        opts.body = JSON.stringify(body);
    }
    const resp = await fetch(url, opts);
    const data = await resp.json();
    return { status: resp.status, data };
}
function setupProxy(app) {
    // ── Agents ──────────────────────────────────────────────────────────
    app.get('/api/ui/agents', async (_req, res) => {
        try {
            const r = await proxyToBot('GET', '/api/v1/agents', null, botToken());
            res.status(r.status).json(r.data);
        }
        catch (err) {
            res.status(502).json({ error: 'bot API unreachable', detail: err.message });
        }
    });
    app.post('/api/ui/agents', async (req, res) => {
        try {
            const r = await proxyToBot('POST', '/api/v1/agents', req.body, botToken());
            res.status(r.status).json(r.data);
        }
        catch (err) {
            res.status(502).json({ error: 'bot API unreachable', detail: err.message });
        }
    });
    app.delete('/api/ui/agents/:id', async (req, res) => {
        try {
            const r = await proxyToBot('DELETE', `/api/v1/agents/${req.params.id}`, null, botToken());
            res.status(r.status).json(r.data);
        }
        catch (err) {
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
        }
        catch (err) {
            res.status(502).json({ error: 'bot API unreachable', detail: err.message });
        }
    });
    app.put('/api/ui/agents/:id/config', async (req, res) => {
        try {
            const r = await proxyToBot('PUT', `/api/v1/agents/${req.params.id}/config`, req.body, botToken());
            res.status(r.status).json(r.data);
        }
        catch (err) {
            res.status(502).json({ error: 'bot API unreachable', detail: err.message });
        }
    });
    // ── Status ──────────────────────────────────────────────────────────
    app.get('/api/ui/status', async (_req, res) => {
        try {
            const r = await proxyToBot('GET', '/api/v1/health', null, '');
            res.status(r.status).json(r.data);
        }
        catch (err) {
            res.status(502).json({ error: 'bot API unreachable', detail: err.message });
        }
    });
}
