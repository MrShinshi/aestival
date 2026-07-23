"use strict";
/**
 * Conversation reader — reads the bot's SQLite database directly (read-only).
 *
 * Each agent has its own SQLite DB at {storage_dir}/conversations.db.
 */
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.setupConversations = setupConversations;
const better_sqlite3_1 = __importDefault(require("better-sqlite3"));
const fs = __importStar(require("fs"));
// Base path for agent storage dirs
const CONTEXTS_BASE = process.env.BOT_CONTEXTS_BASE || '/home/shinshi/aestival/bin/contexts';
function openDb(agentId) {
    // For now, each agent DB is at contexts/{agentId}/conversations.db
    // or the legacy contexts/conversations.db for "default" agent
    const paths = [
        `${CONTEXTS_BASE}/${agentId}/conversations.db`,
        `${CONTEXTS_BASE}/conversations.db`,
    ];
    for (const p of paths) {
        if (fs.existsSync(p)) {
            return new better_sqlite3_1.default(p, { readonly: true });
        }
    }
    return null;
}
function setupConversations(app) {
    // ── List conversations (aggregated across agents) ───────────────────
    app.get('/api/ui/conversations', (req, res) => {
        try {
            // For now, query the default DB.  Multi-agent support will
            // iterate over all agent directories.
            const db = openDb('default');
            if (!db) {
                res.json({ conversations: [] });
                return;
            }
            const limit = Math.min(parseInt(req.query.limit) || 20, 100);
            const stmt = db.prepare(`
        SELECT convo_id, COUNT(*) as msg_count,
               MIN(created_at) as first_at, MAX(created_at) as last_at
        FROM messages
        GROUP BY convo_id
        ORDER BY last_at DESC
        LIMIT ?
      `);
            const rows = stmt.all(limit);
            const conversations = rows.map((r) => ({
                convo_id: r.convo_id,
                message_count: r.msg_count,
                first_at: new Date(r.first_at).toISOString(),
                last_at: new Date(r.last_at).toISOString(),
            }));
            db.close();
            res.json({ conversations });
        }
        catch (err) {
            res.status(500).json({ error: err.message });
        }
    });
    // ── Conversation detail ─────────────────────────────────────────────
    app.get('/api/ui/conversations/:id', (req, res) => {
        try {
            const db = openDb('default');
            if (!db) {
                res.status(404).json({ error: 'database not found' });
                return;
            }
            const stmt = db.prepare(`
        SELECT role, nick, content, tool_calls_json, created_at
        FROM messages
        WHERE convo_id = ?
        ORDER BY created_at, id
      `);
            const rows = stmt.all(req.params.id);
            const messages = rows.map((r) => ({
                role: r.role,
                nick: r.nick || undefined,
                content: r.content,
                tool_calls: r.tool_calls_json ? JSON.parse(r.tool_calls_json) : undefined,
                created_at: new Date(r.created_at).toISOString(),
            }));
            db.close();
            res.json({ convo_id: req.params.id, messages });
        }
        catch (err) {
            res.status(500).json({ error: err.message });
        }
    });
}
