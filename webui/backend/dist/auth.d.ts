/**
 * Auto-login — no OAuth required.
 *
 * Requires environment variable:
 *   JWT_SECRET  (shared with bot management_api.jwt_secret)
 */
import { Express } from 'express';
export declare function setupAuth(app: Express): void;
