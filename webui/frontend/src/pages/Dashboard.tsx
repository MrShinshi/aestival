import { useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import {
  LayoutDashboard, Cpu, BarChart3, Activity, MessageSquare,
  Wrench, Zap, Clock,
} from 'lucide-react';
import { api, type AgentInfo } from '../lib/api';
import { useMetricsHistory } from '../hooks/useMetricsHistory';
import StatCard from '../components/StatCard';
import ResourceChart from '../components/ResourceChart';
import TokenChart from '../components/TokenChart';
import AgentMetricsTable from '../components/AgentMetricsTable';

type Tab = 'overview' | 'agents' | 'tokens';

const tabs: { key: Tab; label: string; icon: React.ComponentType<{ size?: number }> }[] = [
  { key: 'overview', label: '概览', icon: LayoutDashboard },
  { key: 'agents', label: 'Agent 资源', icon: Cpu },
  { key: 'tokens', label: 'Token 用量', icon: BarChart3 },
];

function fmtUptime(seconds: number): string {
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  if (d > 0) return `${d}d ${h}h`;
  if (h > 0) return `${h}h ${m}m`;
  return `${m}m`;
}

function fmtNum(n: number): string {
  if (n >= 1_000_000) return `${(n / 1_000_000).toFixed(1)}M`;
  if (n >= 1_000) return `${(n / 1_000).toFixed(1)}K`;
  return String(n);
}

export default function Dashboard() {
  const [tab, setTab] = useState<Tab>('overview');

  const statusQ = useQuery({ queryKey: ['status'], queryFn: api.status, refetchInterval: 10_000 });
  const agentsQ = useQuery({ queryKey: ['agents'], queryFn: api.agents, refetchInterval: 10_000 });
  const metricsQ = useQuery({ queryKey: ['metrics'], queryFn: api.metrics, refetchInterval: 15_000, enabled: tab === 'agents' || tab === 'overview' });
  const tokensQ = useQuery({ queryKey: ['tokenStats'], queryFn: api.tokenStats, refetchInterval: 30_000, enabled: tab === 'tokens' || tab === 'overview' });
  // Real-time CPU / memory history for the dynamic line chart (poll every 5 s)
  const metricsHistory = useMetricsHistory(60);

  const status = statusQ.data;
  const agents: AgentInfo[] = agentsQ.data || [];
  const metrics = metricsQ.data;
  const tokens = tokensQ.data || [];

  const error = statusQ.error || agentsQ.error;
  const runningCount = agents.filter(a => a.status === 'running').length;
  const errorCount = agents.filter(a => a.status === 'error').length;

  // Token aggregate for the overview tab
  const totalPrompt = tokens.reduce((s, t) => s + t.prompt_tokens, 0);
  const totalCompletion = tokens.reduce((s, t) => s + t.completion_tokens, 0);
  const totalRequests = tokens.reduce((s, t) => s + t.requests, 0);

  return (
    <div>
      <h2 className="text-xl font-bold mb-6">仪表盘</h2>

      {error && (
        <div className="bg-red-900/50 text-red-400 p-3 rounded mb-4 text-sm">
          {(error as Error).message}
        </div>
      )}

      {/* ── Tabs ─────────────────────────────────────────────────────────── */}
      <div className="flex gap-1 mb-6 border-b border-gray-800">
        {tabs.map(t => (
          <button
            key={t.key}
            onClick={() => setTab(t.key)}
            className={`flex items-center gap-1.5 px-4 py-2 text-sm rounded-t transition-colors ${
              tab === t.key
                ? 'bg-gray-900 text-indigo-400 border border-b-0 border-gray-800'
                : 'text-gray-500 hover:text-gray-300'
            }`}
          >
            <t.icon size={14} />
            {t.label}
          </button>
        ))}
      </div>

      {/* ── Tab: Overview ────────────────────────────────────────────────── */}
      {tab === 'overview' && (
        <div className="space-y-6">
          {/* Stat cards row */}
          <div className="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-5 gap-3">
            <StatCard label="Agent 总数" value={status?.agent_count || 0} color="indigo" icon={Activity} />
            <StatCard label="运行中" value={runningCount} color="green" icon={Zap} />
            <StatCard label="异常" value={errorCount} color={errorCount > 0 ? 'red' : 'gray'} icon={Activity} />
            <StatCard
              label="正常运行时间"
              value={status ? fmtUptime(status.uptime_seconds) : '--'}
              color="blue"
              icon={Clock}
            />
            <StatCard
              label="总消息数"
              value={metrics ? fmtNum(metrics.aggregate.total_messages) : '--'}
              color="yellow"
              icon={MessageSquare}
            />
          </div>

          {/* CPU + Memory side by side */}
          <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
            {/* CPU line chart */}
            <div className="rounded-lg border border-gray-800 bg-gray-900 p-4">
              <div className="flex items-center justify-between mb-3">
                <h3 className="text-sm font-semibold text-gray-400">CPU</h3>
                <span className="text-xs text-gray-500">
                  当前 <span className="text-indigo-400 font-mono">{status?.system?.cpu_percent?.toFixed(1) ?? '--'}%</span>
                </span>
              </div>
              <ResourceChart
                data={metricsHistory}
                series={[{ dataKey: 'cpuPercent', name: 'CPU', color: '#818cf8', asPercent: true, domain: [0, 100] }]}
                height={180}
              />
            </div>

            {/* Memory line chart — % of total system memory */}
            <div className="rounded-lg border border-gray-800 bg-gray-900 p-4">
              <div className="flex items-center justify-between mb-3">
                <h3 className="text-sm font-semibold text-gray-400">内存</h3>
                <span className="text-xs text-gray-500">
                  当前 <span className="text-green-400 font-mono">{(() => {
                    const rss = status?.system?.memory_rss_mb || 0;
                    const total = status?.system?.memory_total_mb || 0;
                    const pct = total > 0 ? ((rss / total) * 100).toFixed(1) : '--';
                    const label = rss >= 1024 ? `${(rss / 1024).toFixed(1)} GB` : `${rss.toFixed(0)} MB`;
                    return `${label} (${pct}%)`;
                  })()}</span>
                </span>
              </div>
              <ResourceChart
                data={metricsHistory}
                series={[{ dataKey: 'memoryPercent', name: '内存占比', color: '#4ade80', asPercent: true, domain: [0, 100] }]}
                height={180}
              />
            </div>
          </div>

          {/* Thread / Worker info */}
          <div className="grid grid-cols-4 gap-2 text-xs text-gray-500">
            <div>系统线程: <span className="text-gray-300">{status?.system?.thread_count ?? '--'}</span></div>
            <div>Worker 槽: <span className="text-gray-300">{metrics?.workers?.total_slots ?? '--'}</span></div>
            <div>队列深度: <span className="text-gray-300">{metrics?.workers?.total_queue_depth ?? '--'}</span></div>
            <div>Agent 线程数: <span className="text-gray-300">{agents.length > 0 ? `~${agents.length * 2}+` : '--'}</span></div>
          </div>

          {/* Mini token chart (7 days) */}
          {tokens.length > 0 && (
            <div className="rounded-lg border border-gray-800 bg-gray-900 p-4">
              <h3 className="text-sm font-semibold text-gray-400 mb-3">近 7 天 Token 用量</h3>
              <TokenChart data={tokens} days={7} />
            </div>
          )}
        </div>
      )}

      {/* ── Tab: Agent Resources ─────────────────────────────────────────── */}
      {tab === 'agents' && (
        <div className="space-y-4">
          {/* Summary cards */}
          <div className="grid grid-cols-2 sm:grid-cols-4 gap-3">
            <StatCard
              label="Prompt Tokens"
              value={metrics ? fmtNum(metrics.aggregate.total_prompt_tokens) : '--'}
              color="indigo"
              icon={Wrench}
            />
            <StatCard
              label="Completion Tokens"
              value={metrics ? fmtNum(metrics.aggregate.total_completion_tokens) : '--'}
              color="green"
              icon={Wrench}
            />
            <StatCard
              label="工具调用"
              value={metrics ? fmtNum(metrics.aggregate.total_tool_calls) : '--'}
              color="yellow"
              icon={Zap}
            />
            <StatCard
              label="Worker 槽位"
              value={metrics?.workers?.total_slots ?? '--'}
              color="blue"
              icon={Cpu}
            />
          </div>

          {/* Agent table */}
          <div className="rounded-lg border border-gray-800 bg-gray-900 p-4">
            <h3 className="text-sm font-semibold text-gray-400 mb-3">Agent 资源明细</h3>
            {metricsQ.isLoading ? (
              <div className="text-gray-500 text-sm">Loading...</div>
            ) : (
              <AgentMetricsTable agents={metrics?.agents || []} />
            )}
          </div>
        </div>
      )}

      {/* ── Tab: Token Usage ─────────────────────────────────────────────── */}
      {tab === 'tokens' && (
        <div className="space-y-4">
          <div className="grid grid-cols-2 sm:grid-cols-3 gap-3">
            <StatCard label="本月 Prompt" value={fmtNum(totalPrompt)} color="indigo" />
            <StatCard label="本月 Completion" value={fmtNum(totalCompletion)} color="green" />
            <StatCard label="本月请求数" value={fmtNum(totalRequests)} color="blue" />
          </div>

          <div className="rounded-lg border border-gray-800 bg-gray-900 p-4">
            <h3 className="text-sm font-semibold text-gray-400 mb-3">30 天 Token 用量趋势</h3>
            {tokensQ.isLoading ? (
              <div className="text-gray-500 text-sm">Loading...</div>
            ) : (
              <TokenChart data={tokens} days={30} />
            )}
          </div>

          {/* Raw data table */}
          {tokens.length > 0 && (
            <div className="rounded-lg border border-gray-800 bg-gray-900 p-4 overflow-x-auto">
              <h3 className="text-sm font-semibold text-gray-400 mb-3">每日明细</h3>
              <table className="w-full text-xs">
                <thead>
                  <tr className="text-left text-gray-500 border-b border-gray-800">
                    <th className="pb-2 pr-4">日期</th>
                    <th className="pb-2 pr-4 text-right">请求数</th>
                    <th className="pb-2 pr-4 text-right">Prompt Tokens</th>
                    <th className="pb-2 text-right">Completion Tokens</th>
                  </tr>
                </thead>
                <tbody>
                  {[...tokens].reverse().map(t => (
                    <tr key={t.date} className="border-b border-gray-800/50 text-gray-300">
                      <td className="py-1.5 pr-4">{t.date}</td>
                      <td className="py-1.5 pr-4 text-right">{t.requests.toLocaleString()}</td>
                      <td className="py-1.5 pr-4 text-right">{t.prompt_tokens.toLocaleString()}</td>
                      <td className="py-1.5 text-right">{t.completion_tokens.toLocaleString()}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </div>
      )}
    </div>
  );
}
