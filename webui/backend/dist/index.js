"use strict";
/**
 * aestival Web UI Backend
 *
 * Express server that:
 * 1. Auto-login (no OAuth required)
 * 2. Issues JWT tokens for the bot's internal API
 * 3. Proxies management requests to the bot's internal API (127.0.0.1:9090)
 * 4. Reads bot log files and SQLite databases directly (read-only)
 * 5. Serves frontend static files
 */
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
const express_1 = __importDefault(require("express"));
const cors_1 = __importDefault(require("cors"));
const path_1 = __importDefault(require("path"));
const auth_1 = require("./auth");
const proxy_1 = require("./proxy");
const logs_1 = require("./logs");
const conversations_1 = require("./conversations");
const app = (0, express_1.default)();
const PORT = parseInt(process.env.PORT || '3000', 10);
// Trust Nginx reverse proxy
app.set('trust proxy', 1);
app.use((0, cors_1.default)({
    origin: process.env.CORS_ORIGIN || 'http://localhost:5173',
    credentials: true,
}));
app.use(express_1.default.json({ limit: '1mb' }));
// Routes
(0, auth_1.setupAuth)(app);
(0, proxy_1.setupProxy)(app);
(0, logs_1.setupLogs)(app);
(0, conversations_1.setupConversations)(app);
// Health check
app.get('/api/ui/health', (_req, res) => {
    res.json({ status: 'ok', timestamp: new Date().toISOString() });
});
// Serve frontend static files in production
const frontendDist = path_1.default.resolve(__dirname, '../../frontend/dist');
app.use(express_1.default.static(frontendDist));
// SPA fallback — serve index.html for any unmatched route
app.get('*', (_req, res) => {
    res.sendFile(path_1.default.join(frontendDist, 'index.html'));
});
app.listen(PORT, () => {
    console.log(`aestival Web UI backend listening on http://localhost:${PORT}`);
});
