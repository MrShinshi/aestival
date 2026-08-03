/**
 * Typed API client for the bot management API.
 * All requests include the JWT Bearer token via httpOnly cookie.
 */

const BASE = '/api/ui';
const TIMEOUT_MS = 30_000;

export interface RequestOpts {
  skipAuthRedirect?: boolean;
}

async function request<T>(method: string, path: string, body?: unknown, opts?: RequestOpts): Promise<T> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), TIMEOUT_MS);

  const fetchOpts: RequestInit = {
    method,
    headers: body ? { 'Content-Type': 'application/json' } : undefined,
    credentials: 'include',
    signal: controller.signal,
  };
  if (body) fetchOpts.body = JSON.stringify(body);

  try {
    const resp = await fetch(BASE + path, fetchOpts);
    clearTimeout(timer);

    if (resp.status === 401 && !opts?.skipAuthRedirect) {
      // Dispatch event so AuthContext can clear state; ProtectedRoute handles redirect.
      window.dispatchEvent(new CustomEvent('auth:expired'));
      throw new Error('authentication expired');
    }

    if (!resp.ok) {
      const err = await resp.json().catch(() => ({ error: resp.statusText }));
      throw new Error(err.error || resp.statusText);
    }
    return resp.json();
  } finally {
    clearTimeout(timer);
  }
}

// ── Types ──────────────────────────────────────────────────────────────

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

export interface SystemMetrics {
  /** Process CPU 0–100, per-core normalised (recent delta). */
  cpu_percent: number;
  cpu_percent_recent?: number;
  /** Host-wide CPU usage 0–100. */
  system_cpu_percent?: number;
  /** Process RSS in MB. */
  memory_rss_mb: number;
  memory_total_mb?: number;
  /** Host-wide used physical RAM in MB. */
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

export interface AgentMetricsDetail {
  message_count: number;
  tool_call_count: number;
  prompt_tokens: number;
  completion_tokens: number;
  last_message_at?: string;
  started_at?: string;
  uptime_seconds?: number;
}

export interface AgentWithMetrics {
  id: string;
  status: string;
  metrics: AgentMetricsDetail;
  workers?: { active_slots: number; queue_depth: number };
  last_error?: string;
}

export interface MetricsResponse {
  status: string;
  system: SystemMetrics;
  workers: { total_slots: number; total_queue_depth: number };
  agents: AgentWithMetrics[];
  aggregate: {
    total_messages: number;
    total_tool_calls: number;
    total_prompt_tokens: number;
    total_completion_tokens: number;
  };
}

export interface PluginInfo {
  name: string;
  display_name: string;
  description: string;
  version?: string;
  enabled: boolean;
  config?: Record<string, unknown>;
}

export interface TokenStat {
  date: string;
  requests: number;
  prompt_tokens: number;
  completion_tokens: number;
}

export interface LogResult {
  lines: string[];
  total: number;
  returned: number;
}

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

export interface AgentCreateResult {
  id: string;
}

export interface AgentActionResult {
  id: string;
  action: string;
}

// ── API methods ────────────────────────────────────────────────────────

export const api = {
  // Status
  status: () => request<BotStatus>('GET', '/status'),

  // Agents
  agents: async (): Promise<AgentInfo[]> => {
    const r = await request<{ data: AgentInfo[] } | AgentInfo[]>('GET', '/agents');
    return Array.isArray(r) ? r : (r.data || []);
  },
  createAgent: (cfg: {
    id: string;
    name: string;
    platform?: string;
    qq_app_id?: string;
    qq_app_secret?: string;
    llm_provider?: string;
    deepseek_api_key?: string;
    deepseek_model?: string;
    openai_api_key?: string;
    openai_model?: string;
  }) =>
    request<AgentCreateResult>('POST', '/agents', cfg),
  deleteAgent: (id: string) =>
    request<AgentActionResult>('DELETE', `/agents/${id}`),
  agentAction: (id: string, action: 'start' | 'stop') =>
    request<AgentActionResult>('POST', `/agents/${id}/${action}`),
  updateAgentConfig: (id: string, cfg: { name?: string; enabled?: boolean; llm_provider?: string; workspace?: string; mode?: string }) =>
    request<AgentActionResult>('PUT', `/agents/${id}/config`, cfg),

  // Logs
  logs: (level?: string, limit?: number) =>
    request<LogResult>('GET', `/logs?level=${encodeURIComponent(level || '')}&limit=${limit || 100}`),

  // Conversations
  conversations: (limit?: number) =>
    request<{ conversations: ConversationSummary[] }>('GET', `/conversations?limit=${limit || 20}`),
  conversation: (id: string, agentId?: string) =>
    request<ConversationDetail>('GET', `/conversations/${encodeURIComponent(id)}?agent=${encodeURIComponent(agentId || 'default')}`),

  // Metrics & monitoring (admin only)
  metrics: () => request<MetricsResponse>('GET', '/metrics'),
  metricsHistory: () => request<{ snapshots: Array<BotStatus & { _ts: number }> }>('GET', '/metrics/history'),
  agentMetrics: (id: string) => request<AgentWithMetrics>('GET', `/agents/${id}/metrics`),
  // Plugins
  plugins: async (agentId?: string): Promise<PluginInfo[]> => {
    const r = await request<{ plugins: PluginInfo[] }>('GET',
      `/plugins?agent=${encodeURIComponent(agentId || 'default')}`);
    return r.plugins || [];
  },
  togglePlugin: (name: string, enabled: boolean) =>
    request<{ name: string; enabled: boolean }>('PUT',
      `/plugins/${encodeURIComponent(name)}`, { enabled }),
  pluginConfig: (name: string) =>
    request<PluginInfo>('GET', `/plugins/${encodeURIComponent(name)}/config`),

  tokenStats: async (): Promise<TokenStat[]> => {
    const r = await request<TokenStat[] | { data: TokenStat[] }>('GET', '/tokens');
    if (Array.isArray(r)) return r;
    if (r && typeof r === 'object' && 'data' in r && Array.isArray(r.data)) return r.data;
    return [];
  },
};

// ── Auth API (credential-based) ─────────────────────────────────────────────
//
// login/register skip the 401 auto-redirect so that invalid credentials
// return a typed error response rather than forcing a page navigation.

export const auth = {
  login: (username: string, password: string) =>
    request<{ success: boolean; user: { id: string; username: string; avatar_url: string } }>(
      'POST', '/auth/login', { username, password }, { skipAuthRedirect: true },
    ),
  register: (username: string, password: string) =>
    request<{ success: boolean; user: { id: string; username: string; avatar_url: string } }>(
      'POST', '/auth/register', { username, password }, { skipAuthRedirect: true },
    ),
  setPassword: (password: string) =>
    request<{ success: boolean }>('POST', '/auth/set-password', { password }),
};

/** Current app version — injected by Vite at build time. */
export const APP_VERSION: string = (typeof __APP_VERSION__ !== 'undefined' ? __APP_VERSION__ : '1.0.0') as string;
