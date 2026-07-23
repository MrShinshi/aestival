/**
 * Proxy requests to the bot's internal management API (127.0.0.1:9090).
 *
 * The Web UI backend adds the JWT Bearer token to every proxied request.
 * The bot's management_api verifies this token.
 */
import { Express } from 'express';
export declare function setupProxy(app: Express): void;
