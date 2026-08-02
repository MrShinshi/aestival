import { useQuery } from '@tanstack/react-query';
import {
  Activity, Bot, MessageSquare, Clock, ServerCog,
} from 'lucide-react';
import { api, APP_VERSION, type AgentInfo } from '../lib/api';
import { useUptime, fmtUptime } from '../hooks/useUptime';
import StatCard from '../components/StatCard';

const platformLabel: Record<string, string> = {
  qq: 'QQ',
  console: '控制台',
};

function statusBadge(status: string) {
  const map: Record<string, { text: string; cls: string }> = {
    running: { text: '运行中', cls: 'bg-green-900/40 text-green-400' },
    starting: { text: '启动中', cls: 'bg-yellow-900/40 text-yellow-400' },
    stopping: { text: '停止中', cls: 'bg-yellow-900/40 text-yellow-400' },
    error: { text: '异常', cls: 'bg-red-900/40 text-red-400' },
    stopped: { text: '已停止', cls: 'bg-gray-800 text-gray-400' },
  };
  const m = map[status] || map.stopped;
  return (
    <span className={`px-2 py-0.5 rounded text-xs whitespace-nowrap ${m.cls}`}>
      {m.text}
    </span>
  );
}

export default function Status() {
  const statusQ = useQuery({ queryKey: ['status'], queryFn: api.status, refetchInterval: 10_000 });
  const healthQ = useQuery({ queryKey: ['health'], queryFn: api.health, refetchInterval: 30_000 });
  const agentsQ = useQuery({ queryKey: ['agents'], queryFn: api.agents, refetchInterval: 10_000 });

  const status = statusQ.data;
  const health = healthQ.data;
  const agents: AgentInfo[] = agentsQ.data || [];
  const liveUptime = useUptime(status?.uptime_seconds);

  const runningCount = agents.filter(a => a.status === 'running').length;
  const errorCount = agents.filter(a => a.status === 'error').length;
  const error = statusQ.error || agentsQ.error || healthQ.error;

  return (
    <div>
      <h2 className="text-xl font-bold mb-6">状态</h2>

      {error && (
        <div className="bg-red-900/50 text-red-400 p-3 rounded mb-4 text-sm">
          {(error as Error).message}
        </div>
      )}

      {/* ── Health check banner ──────────────────────────────────────────── */}
      <div className={`rounded-lg border p-4 mb-6 flex items-center justify-between ${
        health?.checks?.botApi === 'ok'
          ? 'border-green-800 bg-green-950/30'
          : 'border-red-800 bg-red-950/30'
      }`}>
        <div className="flex items-center gap-2 text-sm">
          <ServerCog size={16} className="text-gray-400" aria-hidden="true" />
          <span className="text-gray-300">Bot 服务</span>
        </div>
        <div className="flex items-center gap-3 text-sm">
          <span className={health?.checks?.botApi === 'ok' ? 'text-green-400' : 'text-red-400'}>
            {health?.checks?.botApi === 'ok' ? '正常' : (health?.checks?.botApi || '检查中…')}
          </span>
          {health?.timestamp && (
            <span className="text-xs text-gray-500">
              {new Date(health.timestamp).toLocaleTimeString()}
            </span>
          )}
        </div>
      </div>

      {/* ── Stat cards row ───────────────────────────────────────────────── */}
      <div className="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-5 gap-3 mb-6">
        <StatCard label="Agent 总数" value={agents.length} color="indigo" icon={Activity} />
        <StatCard label="运行中" value={runningCount} color="green" icon={Bot} />
        <StatCard label="异常" value={errorCount} color={errorCount > 0 ? 'red' : 'gray'} icon={Activity} />
        <StatCard
          label="正常运行时间"
          value={liveUptime !== undefined ? fmtUptime(liveUptime) : '--'}
          color="blue"
          icon={Clock}
        />
        <StatCard label="版本" value={`v${APP_VERSION}`} color="gray" />
      </div>

      {/* ── Agent list (read-only) ───────────────────────────────────────── */}
      <div className="rounded-lg border border-gray-800 bg-gray-900 p-4">
        <h3 className="text-sm font-semibold text-gray-400 mb-3">Agent 列表</h3>
        {agentsQ.isLoading ? (
          <div className="text-gray-500 text-sm">Loading...</div>
        ) : agents.length === 0 ? (
          <div className="text-gray-500 text-sm">暂无 Agent</div>
        ) : (
          <div className="space-y-2">
            {agents.map(a => (
              <div
                key={a.id}
                className="flex items-center gap-3 p-3 rounded-lg border border-gray-800 bg-gray-950/40"
              >
                <div className="flex-1 min-w-0">
                  <div className="flex items-center gap-2">
                    <span className="text-sm font-semibold text-gray-200 truncate">
                      {a.name || a.id}
                    </span>
                    <span className="text-xs text-gray-500">
                      {platformLabel[a.platform] || a.platform}
                    </span>
                  </div>
                  <div className="flex items-center gap-3 mt-0.5 text-xs text-gray-500">
                    <span className="flex items-center gap-1">
                      <MessageSquare size={11} aria-hidden="true" />
                      {a.message_count.toLocaleString()}
                    </span>
                    {a.bot_nick && <span>昵称: {a.bot_nick}</span>}
                  </div>
                </div>
                {a.status === 'error' && a.last_error && (
                  <div className="hidden md:block max-w-xs truncate text-xs text-red-400/80" title={a.last_error}>
                    {a.last_error}
                  </div>
                )}
                {statusBadge(a.status)}
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}
