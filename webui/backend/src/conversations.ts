/**
 * Conversation reader — reads the bot's SQLite database directly (read-only).
 *
 * Each agent has its own SQLite DB at {storage_dir}/conversations.db.
 */

import { Express, Request, Response } from 'express';
import { requireAuth } from './auth';
import Database from 'better-sqlite3';
import * as fs from 'fs';

// Base path for agent storage dirs
const CONTEXTS_BASE = process.env.BOT_CONTEXTS_BASE || '/home/shinshi/aestival/bin/contexts';

function openDb(agentId: string): Database.Database | null {
  // For now, each agent DB is at contexts/{agentId}/conversations.db
  // or the legacy contexts/conversations.db for "default" agent
  const paths = [
    `${CONTEXTS_BASE}/${agentId}/conversations.db`,
    `${CONTEXTS_BASE}/conversations.db`,
  ];
  for (const p of paths) {
    if (fs.existsSync(p)) {
      return new Database(p, { readonly: true });
    }
  }
  return null;
}

export function setupConversations(app: Express) {
  // ── List conversations (aggregated across agents) ───────────────────
  app.get('/api/ui/conversations', requireAuth, (req, res) => {
    try {
      // For now, query the default DB.  Multi-agent support will
      // iterate over all agent directories.
      const db = openDb('default');
      if (!db) {
        res.json({ conversations: [] });
        return;
      }

      const limit = Math.min(parseInt(req.query.limit as string) || 20, 100);
      const stmt = db.prepare(`
        SELECT convo_id, COUNT(*) as msg_count,
               MIN(created_at) as first_at, MAX(created_at) as last_at
        FROM messages
        GROUP BY convo_id
        ORDER BY last_at DESC
        LIMIT ?
      `);
      const rows = stmt.all(limit);

      const conversations = rows.map((r: any) => ({
        convo_id: r.convo_id,
        message_count: r.msg_count,
        first_at: new Date(r.first_at).toISOString(),
        last_at: new Date(r.last_at).toISOString(),
      }));

      db.close();
      res.json({ conversations });
    } catch (err: any) {
      res.status(500).json({ error: err.message });
    }
  });

  // ── Conversation detail ─────────────────────────────────────────────
  app.get('/api/ui/conversations/:id', requireAuth, (req, res) => {
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

      const messages = rows.map((r: any) => ({
        role: r.role,
        nick: r.nick || undefined,
        content: r.content,
        tool_calls: r.tool_calls_json ? JSON.parse(r.tool_calls_json) : undefined,
        created_at: new Date(r.created_at).toISOString(),
      }));

      db.close();
      res.json({ convo_id: req.params.id, messages });
    } catch (err: any) {
      res.status(500).json({ error: err.message });
    }
  });
}
