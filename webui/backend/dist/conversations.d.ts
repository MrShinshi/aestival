/**
 * Conversation reader — reads the bot's SQLite database directly (read-only).
 *
 * Each agent has its own SQLite DB at {storage_dir}/conversations.db.
 */
import { Express } from 'express';
export declare function setupConversations(app: Express): void;
