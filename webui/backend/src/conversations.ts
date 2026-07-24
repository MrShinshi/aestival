/**
 * Conversation reader — reads the bot's SQLite database directly (read-only).
 *
 * Each agent has its own SQLite DB at {storage_dir}/conversations.db.
 */

import { Express } from 'express';
import Database from 'better-sqlite3';
import * as fs from 'fs';
import { sanitizeAgentId } from './sanitize';

// Base path for agent storage dirs
const CONTEXTS_BASE = process.env.BOT_CONTEXTS_BASE || '/home/shinshi/aestival/bin/contexts';

/** Make a convo_id human-readable when no sender nick is available. */
function formatConvoId(convoId: string): string {
  // c2c:<openid> → "私聊"
  if (convoId.startsWith('c2c:')) return '私聊';
  // group:<group_id> → "群聊"
  if (convoId.startsWith('group:')) return '群聊';
  // guild:<guild_id>:<channel_id> → "频道"
  if (convoId.startsWith('guild:') || convoId.startsWith('dm:')) return '频道';
  return convoId;
}

/** Ensure a string is safe for JSON serialisation. */
function safeUtf8(s: string): string {
  // better-sqlite3 returns strings from TEXT columns.  If the database
  // contains bytes that aren't valid UTF-8, sqlite3_column_text may
  // produce a string with embedded NULs or invalid code points.
  // Remove NUL bytes and strip lone surrogates.
  return s.replace(/\0/g, '').replace(/[\uD800-\uDFFF]/g, '');
}

function openDb(agentId: string): Database.Database | null {
  // For now, each agent DB is at contexts/{agentId}/conversations.db
  // or the legacy contexts/conversations.db for "default" agent
  const safeId = sanitizeAgentId(agentId);
  const paths = [
    `${CONTEXTS_BASE}/${safeId}/conversations.db`,
    safeId === 'default' ? `${CONTEXTS_BASE}/conversations.db` : null,
  ].filter(Boolean) as string[];

  for (const p of paths) {
    if (fs.existsSync(p)) {
      return new Database(p, { readonly: true });
    }
  }
  return null;
}

export function setupConversations(app: Express) {
  // ── List conversations (aggregated across agents) ───────────────────
  app.get('/api/ui/conversations', (req, res) => {
    const limit = Math.min(parseInt(req.query.limit as string) || 20, 100);

    try {
      const db = openDb('default');
      if (!db) {
        res.json({ conversations: [] });
        return;
      }

      try {
        // Fetch conversation summaries with a human-friendly title derived
        // from the first user message's sender nick (or the convo_id itself).
        const stmt = db.prepare(`
          SELECT m.convo_id,
                 COUNT(*) as msg_count,
                 MIN(m.created_at) as first_at,
                 MAX(m.created_at) as last_at,
                 (SELECT m2.nick FROM messages m2
                  WHERE m2.convo_id = m.convo_id AND m2.role = 'user'
                  ORDER BY m2.created_at LIMIT 1) as title
          FROM messages m
          GROUP BY m.convo_id
          ORDER BY last_at DESC
          LIMIT ?
        `);
        const rows = stmt.all(limit);

        const conversations = rows.map((r: any) => ({
          convo_id: r.convo_id,
          message_count: r.msg_count,
          first_at: new Date(r.first_at).toISOString(),
          last_at: new Date(r.last_at).toISOString(),
          title: r.title || formatConvoId(r.convo_id),
        }));

        res.json({ conversations });
      } finally {
        db.close();
      }
    } catch (err: any) {
      console.error('[conversations] list failed:', err);
      res.status(500).json({ error: 'Internal server error' });
    }
  });

  // ── Conversation detail ─────────────────────────────────────────────
  app.get('/api/ui/conversations/:id', (req, res) => {
    const convoId = req.params.id;
    // Reject obvious path traversal in the conversation id itself.
    if (convoId.includes('..') || convoId.includes('/') || convoId.includes('\\')) {
      res.status(400).json({ error: 'invalid conversation id' });
      return;
    }

    try {
      const db = openDb('default');
      if (!db) {
        res.status(404).json({ error: 'database not found' });
        return;
      }

      try {
        const stmt = db.prepare(`
          SELECT role, nick, content, tool_calls_json, created_at
          FROM messages
          WHERE convo_id = ?
          ORDER BY created_at, id
        `);
        const rows = stmt.all(convoId);

        const messages = rows.map((r: any) => ({
          role: r.role,
          nick: r.nick || undefined,
          content: safeUtf8(r.content),
          tool_calls: r.tool_calls_json ? JSON.parse(r.tool_calls_json) : undefined,
          created_at: new Date(r.created_at).toISOString(),
        }));

        // Derive a title from the first user message's nick or the convo_id format.
        const firstUser = (rows as any[]).find((r: any) => r.role === 'user');
        const title = firstUser?.nick || formatConvoId(convoId);

        res.json({ convo_id: convoId, title, messages });
      } finally {
        db.close();
      }
    } catch (err: any) {
      console.error('[conversations] detail failed:', err);
      res.status(500).json({ error: 'Internal server error' });
    }
  });
}
