/**
 * Shared validation utilities for the Web UI backend.
 */

/** Reject path traversal and invalid characters in agent IDs. */
export function sanitizeAgentId(raw: string): string {
  // Allow only alphanumerics, hyphen, and underscore; cap at 64 chars.
  if (!/^[a-zA-Z0-9_-]{1,64}$/.test(raw)) {
    return '_invalid_';
  }
  return raw;
}
