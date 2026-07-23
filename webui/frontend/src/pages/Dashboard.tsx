import { useState, useEffect } from 'react';
import { api, type AgentInfo, type BotStatus } from '../lib/api';

export default function Dashboard() {
  const [status, setStatus] = useState<BotStatus | null>(null);
  const [agents, setAgents] = useState<AgentInfo[]>([]);
  const [error, setError] = useState('');

  useEffect(() => {
    let alive = true;
    async function load() {
      try {
        const [s, a] = await Promise.all([api.status(), api.agents()]);
        if (alive) { setStatus(s); setAgents(a); }
      } catch (e: any) { if (alive) setError(e.message); }
    }
    load();
    const iv = setInterval(load, 10_000);
    return () => { alive = false; clearInterval(iv); };
  }, []);

  const runningCount = agents.filter(a => a.status === 'running').length;
  const errorCount = agents.filter(a => a.status === 'error').length;

  return (
    <div>
      <h2 className="text-xl font-bold mb-6">仪表盘</h2>
      {error && <div className="bg-red-900/50 text-red-400 p-3 rounded mb-4 text-sm">{error}</div>}
      <div className="grid grid-cols-3 gap-4 mb-8">
        <div className="rounded-lg border border-indigo-800 bg-indigo-950/30 p-4">
          <div className="text-xs text-gray-500 mb-1">Agent 总数</div>
          <div className="text-2xl font-bold">{status?.agent_count || 0}</div>
        </div>
        <div className="rounded-lg border border-green-800 bg-green-950/30 p-4">
          <div className="text-xs text-gray-500 mb-1">运行中</div>
          <div className="text-2xl font-bold">{runningCount}</div>
        </div>
        <div className={`rounded-lg border p-4 ${errorCount > 0 ? 'border-red-800 bg-red-950/30' : 'border-gray-800 bg-gray-900'}`}>
          <div className="text-xs text-gray-500 mb-1">异常</div>
          <div className="text-2xl font-bold">{errorCount}</div>
        </div>
      </div>
      <h3 className="text-lg font-semibold mb-3">Agent 状态</h3>
      {agents.map(a => (
        <div key={a.id} className="flex items-center gap-3 bg-gray-900 rounded-lg p-3 mb-2 border border-gray-800">
          <span className={`w-2 h-2 rounded-full ${a.status === 'running' ? 'bg-green-400' : a.status === 'error' ? 'bg-red-400' : 'bg-gray-500'}`} />
          <span className="flex-1 font-medium">{a.name}</span>
          <span className="text-xs text-gray-500">{a.id}</span>
          <span className="text-xs px-2 py-0.5 rounded bg-green-900/50 text-green-400">{a.status}</span>
        </div>
      ))}
    </div>
  );
}
