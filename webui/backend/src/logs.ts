/**
 * Log file reader — reads the bot's log file directly from disk.
 *
 * Query params: ?level=error&limit=100&since=2026-07-01
 */

import { Express, Request, Response } from 'express';
import { requireAuth } from './auth';
import * as fs from 'fs';
import * as path from 'path';

const LOG_PATH = process.env.BOT_LOG_PATH || '/home/shinshi/aestival/bin/bot.log';

export function setupLogs(app: Express) {
  app.get('/api/ui/logs', requireAuth, (req, res) => {
    try {
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
      res.status(500).json({ error: err.message });
    }
  });
}
