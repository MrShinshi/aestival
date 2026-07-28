/**
 * Log file reader — reads the bot's log file from disk via streaming.
 *
 * Query params: ?level=error&limit=100
 */

import { Express } from 'express';
import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';
import * as readline from 'readline';

// Default: ~/aestival/bin/bot.log (override with BOT_LOG_PATH).
const LOG_PATH = process.env.BOT_LOG_PATH || `${os.homedir()}/aestival/bin/bot.log`;
const LOG_BASE = process.env.BOT_LOG_BASE || `${os.homedir()}/aestival`;

/** Reject log paths outside the allowed base directory. */
function isSafeLogPath(filePath: string): boolean {
  const resolved = path.resolve(filePath);
  const base = path.resolve(LOG_BASE);
  if (resolved.includes('..')) return false;
  return resolved.startsWith(base);
}

const LOG_PATH_VALID = isSafeLogPath(LOG_PATH);

/**
 * Stream the last N lines of a log file, optionally filtered by level.
 * Uses a reverse-line scanning approach to avoid reading the entire file
 * into memory — safe for multi-hundred-MB log files.
 */
async function readLogTail(
  filePath: string, level: string, limit: number,
): Promise<{ lines: string[]; total: number }> {
  return new Promise((resolve, reject) => {
    const stat = fs.statSync(filePath);
    const fd = fs.openSync(filePath, 'r');
    const chunkSize = 64 * 1024; // 64 KB
    let position = stat.size;
    const lines: string[] = [];
    let leftover = '';
    let scanned = 0;

    const done = () => {
      fs.closeSync(fd);
      const filtered = level
        ? lines.filter(l => l.includes(`[${level.toUpperCase()}]`))
        : lines;
      resolve({ lines: filtered.slice(-limit), total: filtered.length });
    };

    const processChunk = () => {
      const readSize = Math.min(chunkSize, position);
      if (readSize <= 0) {
        // Handle final leftover
        if (leftover && lines.length < limit * 2) {
          lines.unshift(leftover);
        }
        done();
        return;
      }
      position -= readSize;
      const buf = Buffer.alloc(readSize);
      fs.readSync(fd, buf, 0, readSize, position);
      let chunk = buf.toString('utf-8') + leftover;
      // Split and keep the partial first line as leftover for the next chunk
      const parts = chunk.split('\n');
      leftover = parts.shift() || '';
      // Push in reverse order (we're reading backwards)
      for (let i = parts.length - 1; i >= 0; i--) {
        const line = parts[i].trim();
        if (line) {
          lines.unshift(line);
          scanned++;
        }
      }
      // Stop early if we have enough lines (2x limit for filtering headroom)
      if (scanned >= limit * 2) {
        if (leftover) lines.unshift(leftover);
        done();
        return;
      }
      processChunk();
    };

    try {
      processChunk();
    } catch (err) {
      fs.closeSync(fd);
      reject(err);
    }
  });
}

export function setupLogs(app: Express) {
  app.get('/api/ui/logs', async (req, res) => {
    try {
      if (!LOG_PATH_VALID) {
        res.status(500).json({ error: 'Log path misconfigured' });
        return;
      }

      const level = (req.query.level as string) || '';
      let limit = Math.min(parseInt(req.query.limit as string) || 100, 1000);
      if (isNaN(limit) || limit <= 0) limit = 100;

      if (!fs.existsSync(LOG_PATH)) {
        res.json({ lines: [], message: 'log file not found' });
        return;
      }

      const { lines, total } = await readLogTail(LOG_PATH, level, limit);
      res.json({ lines, total, returned: lines.length });
    } catch (err: any) {
      console.error('[logs] read failed:', err);
      res.status(500).json({ error: 'Internal server error' });
    }
  });
}
