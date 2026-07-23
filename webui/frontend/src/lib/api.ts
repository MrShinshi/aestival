/**
 * Typed API client for the bot management API.
 * All requests include the JWT Bearer token.
 */

import { getStoredToken } from './auth';

const BASE = '/api/ui';

async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
  const token = getStoredToken();
  const headers: Record<string, string> = { 'Content-Type': 'application/json' };
  if (token) headers['Authorization'] = `Bearer ${token}`;

  const opts: RequestInit = { method, headers, credentials: 'include' };
  if (body) opts.body = JSON.stringify(body);

  const resp = await fetch(`${BASE}${path}`, opts);
  if (!resp.ok) {
    const err = await resp.json().catch(() => ({ error: resp.statusText }));
    throw new Error(err.error || resp.statusText);
  }
  return resp.json();
}

// ── Types ──────────────────────────────────────────────────────────────

export interface AgentInfo {
  id: string;
  name: string;
  status: string;
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

// ── API methods ────────────────────────────────────────────────────────

export const api = {
  // Status
  status: () => request<BotStatus>('GET', '/status'),

  // Agents
  agents: () => request<AgentInfo[]>('GET', '/agents'),
  createAgent: (cfg: Record<string, unknown>) => request<any>('POST', '/agents', cfg),
  deleteAgent: (id: string) => request<any>('DELETE', `/agents/${id}`),
  agentAction: (id: string, action: 'start' | 'stop') => request<any>('POST', `/agents/${id}/${action}`),
  updateAgentConfig: (id: string, cfg: Record<string, unknown>) => request<any>('PUT', `/agents/${id}/config`, cfg),

  // Logs
  logs: (level?: string, limit?: number) =>
    request<LogResult>('GET', `/logs?level=${level || ''}&limit=${limit || 100}`),

  // Conversations
  conversations: (limit?: number) =>
    request<{ conversations: ConversationSummary[] }>('GET', `/conversations?limit=${limit || 20}`),
  conversation: (id: string) => request<ConversationDetail>('GET', `/conversations/${encodeURIComponent(id)}`),
};
