import { useState } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { api, type PluginInfo } from '../lib/api';
import { Puzzle, ToggleLeft, ToggleRight, RefreshCw } from 'lucide-react';

export default function Plugins() {
  const queryClient = useQueryClient();
  const [agentId, setAgentId] = useState('default');
  const [actionError, setActionError] = useState<string | null>(null);

  const { data: agents } = useQuery({
    queryKey: ['agents'],
    queryFn: api.agents,
    refetchInterval: 30_000,
  });

  const { data: plugins, isLoading } = useQuery({
    queryKey: ['plugins', agentId],
    queryFn: () => api.plugins(agentId),
    refetchInterval: 15_000,
  });

  const toggleMutation = useMutation({
    mutationFn: ({ name, enabled }: { name: string; enabled: boolean }) =>
      api.togglePlugin(name, enabled),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['plugins', agentId] });
      setActionError(null);
    },
    onError: (err: Error) => setActionError(`操作失败: ${err.message}`),
  });

  if (isLoading) return <div className="text-gray-400">Loading...</div>;

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <h2 className="text-xl font-bold">插件管理</h2>
        <div className="flex items-center gap-3">
          {/* Agent selector */}
          {agents && agents.length > 0 && (
            <select
              value={agentId}
              onChange={(e) => setAgentId(e.target.value)}
              className="bg-gray-800 border border-gray-700 rounded px-3 py-1.5 text-sm text-gray-200"
            >
              {agents.map((a) => (
                <option key={a.id} value={a.id}>
                  {a.bot_nick || a.name || a.id}
                </option>
              ))}
            </select>
          )}
          <button
            onClick={() => queryClient.invalidateQueries({ queryKey: ['plugins', agentId] })}
            className="p-1.5 text-gray-400 hover:text-gray-200 transition-colors"
            title="刷新"
          >
            <RefreshCw size={16} aria-hidden={true} />
          </button>
        </div>
      </div>

      {actionError && (
        <div className="bg-red-900/30 border border-red-800 text-red-400 text-sm rounded px-3 py-2 mb-4">
          {actionError}
          <button
            className="ml-2 text-red-300 hover:text-red-200"
            onClick={() => setActionError(null)}
          >
            ✕
          </button>
        </div>
      )}

      {!plugins || plugins.length === 0 ? (
        <div className="text-gray-500 text-sm">暂无已注册插件。</div>
      ) : (
        <div className="space-y-3">
          {plugins.map((p: PluginInfo) => (
            <div
              key={p.name}
              className="bg-gray-900 rounded-lg border border-gray-800 p-4 flex items-center justify-between"
            >
              <div className="flex items-center gap-3">
                <div className="w-8 h-8 bg-indigo-900/40 rounded flex items-center justify-center">
                  <Puzzle size={16} className="text-indigo-400" aria-hidden={true} />
                </div>
                <div>
                  <div className="font-medium text-gray-200">{p.display_name}</div>
                  <div className="text-xs text-gray-500">
                    <code className="text-gray-600">{p.name}</code>
                    {p.version && <span className="ml-2">v{p.version}</span>}
                  </div>
                  {p.description && (
                    <div className="text-xs text-gray-500 mt-0.5">{p.description}</div>
                  )}
                </div>
              </div>

              <button
                onClick={() =>
                  toggleMutation.mutate({ name: p.name, enabled: !p.enabled })
                }
                disabled={toggleMutation.isPending}
                className={`flex items-center gap-1.5 px-3 py-1.5 rounded text-sm transition-colors ${
                  p.enabled
                    ? 'bg-emerald-900/30 text-emerald-400 hover:bg-emerald-900/50'
                    : 'bg-gray-800 text-gray-500 hover:bg-gray-700 hover:text-gray-300'
                }`}
                title={p.enabled ? '点击禁用' : '点击启用'}
              >
                {p.enabled ? (
                  <ToggleRight size={18} aria-hidden={true} />
                ) : (
                  <ToggleLeft size={18} aria-hidden={true} />
                )}
                {p.enabled ? '已启用' : '已禁用'}
              </button>
            </div>
          ))}
        </div>
      )}

      <div className="mt-4 text-xs text-gray-600">
        更改即时生效，无需重启 Agent。下一条消息到达时即可反映变更。
      </div>
    </div>
  );
}
