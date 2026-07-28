/**
 * Shared validation rules — imported by both backend and frontend.
 *
 * Keep this single source of truth; do NOT duplicate regexes in
 * RegisterPage.tsx, Agents.tsx, or credentials.ts.
 */

/** Allowed characters + length: ASCII word chars, underscore, hyphen, CJK. */
export const USERNAME_RE = /^[\w一-鿿㐀-䶿-]{2,32}$/;

/** Agent ID: alphanumeric, underscore, hyphen. 1–64 chars. */
export const AGENT_ID_RE = /^[a-zA-Z0-9_-]{1,64}$/;

const MIN_PASSWORD_LEN = 8;
const MAX_PASSWORD_LEN = 128;

export function validateUsername(username: unknown): string | null {
  if (!username || typeof username !== 'string') return '用户名不能为空';
  const trimmed = username.trim();
  if (trimmed.length < 2 || trimmed.length > 32) return '用户名长度需在 2-32 个字符之间';
  if (!USERNAME_RE.test(trimmed)) return '用户名只能包含字母、数字、下划线、连字符和中文';
  return null;
}

export function validatePassword(password: unknown): string | null {
  if (!password || typeof password !== 'string') return '密码不能为空';
  if (password.length < MIN_PASSWORD_LEN) return `密码长度至少为 ${MIN_PASSWORD_LEN} 个字符`;
  if (password.length > MAX_PASSWORD_LEN) return `密码长度不能超过 ${MAX_PASSWORD_LEN} 个字符`;
  return null;
}

export function validateAgentId(id: unknown): string | null {
  if (!id || typeof id !== 'string') return 'Agent ID 不能为空';
  if (id.length > 64) return 'Agent ID 过长（最多 64 字符）';
  if (!AGENT_ID_RE.test(id)) return 'Agent ID 只能包含英文、数字、连字符和下划线';
  return null;
}
