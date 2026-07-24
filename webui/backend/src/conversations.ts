/**
 * Conversation reader — reads the bot's SQLite databases directly (read-only).
 *
 * Each agent has its own SQLite DB at {storage_dir}/conversations.db.
 * The list endpoint discovers all available agent DBs and returns
 * conversations grouped by agent.
 */

import { Express } from 'express';
import Database from 'better-sqlite3';
import * as fs from 'fs';
import { sanitizeAgentId } from './sanitize';

// Base path for agent storage dirs
const CONTEXTS_BASE = process.env.BOT_CONTEXTS_BASE || '/home/shinshi/aestival/bin/contexts';

/** Make a convo_id human-readable when no sender nick is available. */
function formatConvoId(convoId: string): string {
  if (convoId.startsWith('c2c:')) return '私聊';
  if (convoId.startsWith('group:')) return '群聊';
  if (convoId.startsWith('guild:') || convoId.startsWith('dm:')) return '频道';
  return convoId;
}

/** Extract the conversation type label from a convo_id. */
function convoType(convoId: string): string {
  if (convoId.startsWith('c2c:')) return '私聊';
  if (convoId.startsWith('group:')) return '群聊';
  if (convoId.startsWith('guild:')) return '频道';
  if (convoId.startsWith('dm:')) return '频道私信';
  return '其他';
}

/** Ensure a string is safe for JSON serialisation and HTML display. */
function safeUtf8(s: string): string {
  return s
    .replace(/\0/g, '')                         // strip NUL bytes
    .replace(/[\uD800-\uDFFF]/g, '')            // strip lone surrogates
    .replace(/\x1b\[[0-9;]*m/g, '')             // strip ANSI escape (SGR) codes
    .replace(/\x1b\[\??[0-9]*[hl]/g, '')        // strip ANSI cursor codes
    .replace(/\x1b\[[0-9]*[A-HJKSTf]/g, '')     // strip ANSI cursor movement
    .replace(/\x1b\][0-9;]*[^\x07]*\x07/g, ''); // strip ANSI OSC sequences
}

function openDb(agentId: string): Database.Database | null {
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

/**
 * Discover all agent IDs that have a conversations.db on disk.
 * Scans the contexts base directory for subdirectories containing
 * conversations.db files, plus the legacy root-level conversations.db
 * treated as agent "default".
 */
function discoverAgents(): string[] {
  const agents: string[] = [];
  try {
    if (fs.existsSync(CONTEXTS_BASE)) {
      for (const entry of fs.readdirSync(CONTEXTS_BASE, { withFileTypes: true })) {
        if (!entry.isDirectory()) continue;
        const dbPath = `${CONTEXTS_BASE}/${entry.name}/conversations.db`;
        if (fs.existsSync(dbPath)) {
          agents.push(entry.name);
        }
      }
    }
    // Legacy: root-level conversations.db → agent "default"
    if (!agents.includes('default') && fs.existsSync(`${CONTEXTS_BASE}/conversations.db`)) {
      agents.push('default');
    }
  } catch {
    // If we can't scan, fall back to default-only
    if (fs.existsSync(`${CONTEXTS_BASE}/conversations.db`)) {
      agents.push('default');
    }
  }
  return agents;
}

export function setupConversations(app: Express) {
  // ── List conversations (aggregated across all agents) ─────────────────
  app.get('/api/ui/conversations', (req, res) => {
    const limit = Math.min(parseInt(req.query.limit as string) || 20, 100);

    try {
      const agentIds = discoverAgents();
      const allConversations: any[] = [];

      for (const agentId of agentIds) {
        const db = openDb(agentId);
        if (!db) continue;

        try {
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

          for (const r of rows as any[]) {
            allConversations.push({
              agent_id: agentId,
              convo_id: r.convo_id,
              convo_type: convoType(r.convo_id),
              message_count: r.msg_count,
              first_at: new Date(r.first_at).toISOString(),
              last_at: new Date(r.last_at).toISOString(),
              title: r.title || formatConvoId(r.convo_id),
            });
          }
        } finally {
          db.close();
        }
      }

      // Sort by last_at descending across all agents
      allConversations.sort((a, b) => b.last_at.localeCompare(a.last_at));

      res.json({ conversations: allConversations });
    } catch (err: any) {
      console.error('[conversations] list failed:', err);
      res.status(500).json({ error: 'Internal server error' });
    }
  });

  // ── Conversation detail ─────────────────────────────────────────────
  app.get('/api/ui/conversations/:id', (req, res) => {
    const convoId = req.params.id;
    const agentId = (req.query.agent as string) || 'default';

    if (convoId.includes('..') || convoId.includes('/') || convoId.includes('\\')) {
      res.status(400).json({ error: 'invalid conversation id' });
      return;
    }

    try {
      const db = openDb(agentId);
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
        const rows = stmt.all(convoId) as any[];

        const messages = rows.map((r: any) => ({
          role: r.role,
          nick: r.nick || undefined,
          content: safeUtf8(r.content),
          tool_calls: r.tool_calls_json ? JSON.parse(r.tool_calls_json) : undefined,
          created_at: new Date(r.created_at).toISOString(),
        }));

        const firstUser = rows.find((r: any) => r.role === 'user');
        const title = firstUser?.nick || formatConvoId(convoId);

        res.json({
          agent_id: agentId,
          convo_id: convoId,
          convo_type: convoType(convoId),
          title,
          messages,
        });
      } finally {
        db.close();
      }
    } catch (err: any) {
      console.error('[conversations] detail failed:', err);
      res.status(500).json({ error: 'Internal server error' });
    }
  });
}
