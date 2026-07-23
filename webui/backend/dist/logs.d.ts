/**
 * Log file reader — reads the bot's log file directly from disk.
 *
 * Query params: ?level=error&limit=100&since=2026-07-01
 */
import { Express } from 'express';
export declare function setupLogs(app: Express): void;
