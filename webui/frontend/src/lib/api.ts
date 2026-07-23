/**
 * Typed API client for the bot management API.
 * All requests include the JWT Bearer token via httpOnly cookie.
 */

const BASE = '/api/ui';
const TIMEOUT_MS = 30_000;

async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), TIMEOUT_MS);

  const opts: RequestInit = {
    method,
    headers: body ? { 'Content-Type': 'application/json' } : undefined,
    credentials: 'include',
    signal: controller.signal,
  };
  if (body) opts.body = JSON.stringify(body);

  try {
    const resp = await fetch(BASE + path, opts);
    clearTimeout(timer);

    if (resp.status === 401) {
      // Token expired — redirect to refresh
      window.location.href = '/api/ui/auth/token';
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
}

export interface BotStatus {
  status: string;
  uptime_seconds: number;
  agent_count: number;
}

export interface LogResult {
  lines: string[];
  total: number;
  returned: number;
}

export interface ConversationSummary {
  convo_id: string;
  message_count: number;
  first_at: string;
  last_at: string;
}

export interface ConversationDetail {
  convo_id: string;
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
  createAgent: (cfg: { id: string; name: string; platform?: string }) =>
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
  conversation: (id: string) =>
    request<ConversationDetail>('GET', `/conversations/${encodeURIComponent(id)}`),
};

/** Current app version — keep in sync with package.json. */
export const APP_VERSION = '1.0.0';
