import { useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { ChevronDown, ChevronRight } from 'lucide-react';
import { api, type AgentWithMetrics, type AgentMetricsDetail } from '../lib/api';
import { useUptime, fmtUptime } from '../hooks/useUptime';

interface AgentMetricsTableProps {
  agents: AgentWithMetrics[];
}

function fmtNum(n: number): string {
  if (n >= 1_000_000) return `${(n / 1_000_000).toFixed(1)}M`;
  if (n >= 1_000) return `${(n / 1_000).toFixed(1)}K`;
  return String(n);
}

function statusBadge(status: string) {
  const colors: Record<string, string> = {
    running: 'bg-green-900/50 text-green-400',
    error: 'bg-red-900/50 text-red-400',
    stopped: 'bg-gray-800 text-gray-400',
    starting: 'bg-yellow-900/50 text-yellow-400',
    stopping: 'bg-yellow-900/50 text-yellow-400',
  };
  return (
    <span className={`text-xs px-2 py-0.5 rounded ${colors[status] || 'bg-gray-800 text-gray-400'}`}>
      {status}
    </span>
  );
}

function ExpandedRow({ agentId }: { agentId: string }) {
  const q = useQuery({
    queryKey: ['agentMetrics', agentId],
    queryFn: () => api.agentMetrics(agentId),
    refetchInterval: 15_000,
  });

  if (q.isLoading) return <div className="text-gray-500 p-2 text-xs">加载中...</div>;
  if (q.error) return <div className="text-red-400 p-2 text-xs">{(q.error as Error).message}</div>;

  const m: AgentMetricsDetail | undefined = q.data?.metrics;
  const liveUptime = useUptime(m?.uptime_seconds);
  if (!m) return null;

  return (
    <div className="grid grid-cols-2 sm:grid-cols-4 gap-3 p-3 bg-gray-950/50 rounded">
      <div><span className="text-xs text-gray-500">消息数</span><div className="text-sm text-gray-200">{m.message_count.toLocaleString()}</div></div>
      <div><span className="text-xs text-gray-500">工具调用</span><div className="text-sm text-gray-200">{m.tool_call_count.toLocaleString()}</div></div>
      <div><span className="text-xs text-gray-500">Prompt Tokens</span><div className="text-sm text-gray-200">{m.prompt_tokens.toLocaleString()}</div></div>
      <div><span className="text-xs text-gray-500">Completion Tokens</span><div className="text-sm text-gray-200">{m.completion_tokens.toLocaleString()}</div></div>
      {m.last_message_at && (
        <div><span className="text-xs text-gray-500">最后消息</span><div className="text-sm text-gray-200">{new Date(m.last_message_at).toLocaleString()}</div></div>
      )}
      {liveUptime !== undefined && (
        <div><span className="text-xs text-gray-500">运行时间</span><div className="text-sm text-gray-200">{fmtUptime(liveUptime)}</div></div>
      )}
    </div>
  );
}

export default function AgentMetricsTable({ agents }: AgentMetricsTableProps) {
  const [expanded, setExpanded] = useState<Set<string>>(new Set());

  const toggle = (id: string) => {
    setExpanded(prev => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  };

  if (agents.length === 0) {
    return <div className="text-gray-500 text-sm p-4">暂无 Agent 数据</div>;
  }

  return (
    <div className="overflow-x-auto">
      <table className="w-full text-sm">
        <thead>
          <tr className="text-left text-xs text-gray-500 border-b border-gray-800">
            <th className="pb-2 pr-2 w-6" />
            <th className="pb-2 pr-3">Agent</th>
            <th className="pb-2 pr-3">状态</th>
            <th className="pb-2 pr-3 text-right">消息</th>
            <th className="pb-2 pr-3 text-right">工具调用</th>
            <th className="pb-2 pr-3 text-right">Prompt T</th>
            <th className="pb-2 pr-3 text-right">Comp. T</th>
            <th className="pb-2 pr-3 text-right">对话槽</th>
            <th className="pb-2 text-right">队列</th>
          </tr>
        </thead>
        <tbody>
          {agents.map(a => {
            const isOpen = expanded.has(a.id);
            return (
              <>
                <tr
                  key={a.id}
                  className="border-b border-gray-800/50 hover:bg-gray-800/30 cursor-pointer"
                  onClick={() => toggle(a.id)}
                >
                  <td className="py-2 pr-2 text-gray-500">
                    {isOpen ? <ChevronDown size={14} /> : <ChevronRight size={14} />}
                  </td>
                  <td className="py-2 pr-3">
                    <div className="font-medium text-gray-200">{a.id}</div>
                  </td>
                  <td className="py-2 pr-3">{statusBadge(a.status)}</td>
                  <td className="py-2 pr-3 text-right text-gray-300">{fmtNum(a.metrics?.message_count || 0)}</td>
                  <td className="py-2 pr-3 text-right text-gray-300">{fmtNum(a.metrics?.tool_call_count || 0)}</td>
                  <td className="py-2 pr-3 text-right text-gray-300">{fmtNum(a.metrics?.prompt_tokens || 0)}</td>
                  <td className="py-2 pr-3 text-right text-gray-300">{fmtNum(a.metrics?.completion_tokens || 0)}</td>
                  <td className="py-2 pr-3 text-right text-gray-400">{a.workers?.active_slots ?? '-'}</td>
                  <td className="py-2 text-right text-gray-400">{a.workers?.queue_depth ?? '-'}</td>
                </tr>
                {isOpen && (
                  <tr key={`${a.id}-expanded`}>
                    <td colSpan={9} className="p-0">
                      <ExpandedRow agentId={a.id} />
                    </td>
                  </tr>
                )}
              </>
            );
          })}
        </tbody>
      </table>
    </div>
  );
}
