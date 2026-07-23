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
export {};
