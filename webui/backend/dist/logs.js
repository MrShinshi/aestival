"use strict";
/**
 * Log file reader — reads the bot's log file directly from disk.
 *
 * Query params: ?level=error&limit=100&since=2026-07-01
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
Object.defineProperty(exports, "__esModule", { value: true });
exports.setupLogs = setupLogs;
const fs = __importStar(require("fs"));
const LOG_PATH = process.env.BOT_LOG_PATH || '/home/shinshi/aestival/bin/bot.log';
function setupLogs(app) {
    app.get('/api/ui/logs', (req, res) => {
        try {
            const level = req.query.level || '';
            const limit = Math.min(parseInt(req.query.limit) || 100, 1000);
            const since = req.query.since || '';
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
        }
        catch (err) {
            res.status(500).json({ error: err.message });
        }
    });
}
