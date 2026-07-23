/**
 * Log file reader — reads the bot's log file directly from disk.
 *
 * Query params: ?level=error&limit=100&since=2026-07-01
 */

import { Express } from 'express';
import * as fs from 'fs';
import * as path from 'path';

const LOG_PATH = process.env.BOT_LOG_PATH || '/home/shinshi/aestival/bin/bot.log';
// Allowed base directory for log files (prevents reading arbitrary files).
const LOG_BASE = process.env.BOT_LOG_BASE || '/home/shinshi/aestival';

/** Reject log paths outside the allowed base directory. */
function isSafeLogPath(filePath: string): boolean {
  const resolved = path.resolve(filePath);
  const base = path.resolve(LOG_BASE);
  if (resolved.includes('..')) return false;
  return resolved.startsWith(base);
}

// Validate at module load time — fail fast.
const LOG_PATH_VALID = isSafeLogPath(LOG_PATH);

export function setupLogs(app: Express) {
  app.get('/api/ui/logs', (req, res) => {
    try {
      if (!LOG_PATH_VALID) {
        res.status(500).json({ error: 'Log path misconfigured' });
        return;
      }

      const level = (req.query.level as string) || '';
      const limit = Math.min(parseInt(req.query.limit as string) || 100, 1000);
      const since = (req.query.since as string) || '';

      if (!fs.existsSync(LOG_PATH)) {
        res.json({ lines: [], message: 'log file not found' });
        return;
      }

      const content = fs.readFileSync(LOG_PATH, 'utf-8');
      const lines = content.split('\n').filter(l => l.trim());

      // Filter by level
      const filtered = level
        ? lines.filter(l => l.includes(`[${level.toUpperCase()}]`))
        : lines;

      // Filter by date
      const sinceFiltered = since
        ? filtered.filter(l => l >= since)
        : filtered;

      // Take last N
      const result = sinceFiltered.slice(-limit);

      res.json({ lines: result, total: filtered.length, returned: result.length });
    } catch (err: any) {
      console.error('[logs] read failed:', err);
      res.status(500).json({ error: 'Internal server error' });
    }
  });
}
