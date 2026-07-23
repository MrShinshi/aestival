import { useQuery } from '@tanstack/react-query';
import { api, type AgentInfo } from '../lib/api';

export default function Dashboard() {
  const status = useQuery({ queryKey: ['status'], queryFn: api.status, refetchInterval: 10_000 });
  const agents = useQuery({ queryKey: ['agents'], queryFn: api.agents, refetchInterval: 10_000 });

  const runningCount = agents.data?.filter((a: AgentInfo) => a.status === 'running').length || 0;
  const errorCount = agents.data?.filter((a: AgentInfo) => a.status === 'error').length || 0;

  const fmtUptime = (s: number) => {
    const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60);
    return `${h}h ${m}m`;
  };

  return (
    <div>
      <h2 className="text-xl font-bold mb-6">仪表盘</h2>

      {/* Stat cards */}
      <div className="grid grid-cols-3 gap-4 mb-8">
        <StatCard label="Agent 总数" value={status.data?.agent_count || 0} color="indigo" />
        <StatCard label="运行中" value={runningCount} color="green" />
        <StatCard label="异常" value={errorCount} color={errorCount > 0 ? 'red' : 'gray'} />
        <StatCard label="在线时长" value={fmtUptime(status.data?.uptime_seconds || 0)} color="gray" />
      </div>

      {/* Agent list */}
      <h3 className="text-lg font-semibold mb-3">Agent 状态</h3>
      <div className="space-y-2">
        {agents.data?.map((a: AgentInfo) => (
          <div key={a.id} className="flex items-center gap-3 bg-gray-900 rounded-lg p-3 border border-gray-800">
            <span className={`w-2 h-2 rounded-full ${
              a.status === 'running' ? 'bg-green-400' :
              a.status === 'error' ? 'bg-red-400' :
              a.status === 'starting' ? 'bg-yellow-400' : 'bg-gray-500'
            }`} />
            <span className="flex-1 font-medium">{a.name}</span>
            <span className="text-xs text-gray-500">{a.id}</span>
            <span className={`text-xs px-2 py-0.5 rounded ${
              a.status === 'running' ? 'bg-green-900/50 text-green-400' :
              a.status === 'error' ? 'bg-red-900/50 text-red-400' : 'bg-gray-800 text-gray-400'
            }`}>{a.status}</span>
            <span className="text-xs text-gray-500">{a.message_count} msgs</span>
          </div>
        ))}
        {(!agents.data || agents.data.length === 0) && (
          <div className="text-gray-500 text-sm py-4 text-center">暂无 Agent</div>
        )}
      </div>
    </div>
  );
}

function StatCard({ label, value, color }: { label: string; value: string | number; color: string }) {
  const colors: Record<string, string> = {
    indigo: 'border-indigo-800 bg-indigo-950/30',
    green: 'border-green-800 bg-green-950/30',
    red: 'border-red-800 bg-red-950/30',
    gray: 'border-gray-800 bg-gray-900',
  };
  return (
    <div className={`rounded-lg border p-4 ${colors[color] || colors.gray}`}>
      <div className="text-xs text-gray-500 mb-1">{label}</div>
      <div className="text-2xl font-bold">{value}</div>
    </div>
  );
}
