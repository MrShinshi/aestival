/**
 * Shared type definitions for the WebUI API contract.
 *
 * Both backend (Express route handlers) and frontend (API client + UI
 * components) should import from here so that a field rename is caught
 * at compile time rather than silently breaking at runtime.
 */

// ── Auth ──────────────────────────────────────────────────────────────────

export interface ApiUser {
  id: string;
  username: string;
  avatar_url: string;
  created_at?: string;
  is_admin?: boolean;
}

export interface LinkedAccount {
  id: string;
  provider: 'github' | 'qq';
  provider_username: string;
  created_at: string;
}

export interface AuthMeResponse {
  authenticated: boolean;
  user?: ApiUser;
  linked_accounts?: LinkedAccount[];
  has_password?: boolean;
}

// ── Agents ────────────────────────────────────────────────────────────────

export interface AgentInfo {
  id: string;
  name: string;
  status: 'running' | 'stopped' | 'starting' | 'stopping' | 'error';
  platform: string;
  enabled: boolean;
  message_count: number;
  last_error?: string;
  bot_nick?: string;
  bot_avatar?: string;
}

export interface AgentCreateResult {
  id: string;
}
export interface AgentActionResult {
  id: string;
  action: string;
}

// ── Status / Metrics ──────────────────────────────────────────────────────

export interface SystemMetrics {
  cpu_percent: number;
  cpu_percent_recent?: number;
  system_cpu_percent?: number;
  memory_rss_mb: number;
  memory_total_mb?: number;
  memory_used_mb?: number;
  memory_virtual_mb?: number;
  thread_count: number;
  uptime_seconds: number;
}

export interface BotStatus {
  status: string;
  uptime_seconds: number;
  agent_count: number;
  agents_running?: number;
  agents_error?: number;
  system?: SystemMetrics;
}

// ── Logs ──────────────────────────────────────────────────────────────────

export interface LogResult {
  lines: string[];
  total: number;
  returned: number;
}

// ── Conversations ─────────────────────────────────────────────────────────

export interface ConversationSummary {
  agent_id: string;
  convo_id: string;
  convo_type: string;
  message_count: number;
  first_at: string;
  last_at: string;
  title?: string;
}

export interface ConversationDetail {
  agent_id: string;
  convo_id: string;
  convo_type: string;
  title?: string;
  messages: Array<{
    role: string;
    nick?: string;
    content: string;
    created_at: string;
  }>;
}

// ── Misc ──────────────────────────────────────────────────────────────────

export interface TokenStat {
  date: string;
  requests: number;
  prompt_tokens: number;
  completion_tokens: number;
}
