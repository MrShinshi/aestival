import { useState } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { api, type AgentInfo } from '../lib/api';

export default function Agents() {
  const queryClient = useQueryClient();
  const { data: agents, isLoading } = useQuery({ queryKey: ['agents'], queryFn: api.agents, refetchInterval: 10_000 });
  const [showCreate, setShowCreate] = useState(false);

  const startMutation = useMutation({
    mutationFn: (id: string) => api.agentAction(id, 'start'),
    onSuccess: () => queryClient.invalidateQueries({ queryKey: ['agents'] }),
  });

  const stopMutation = useMutation({
    mutationFn: (id: string) => api.agentAction(id, 'stop'),
    onSuccess: () => queryClient.invalidateQueries({ queryKey: ['agents'] }),
  });

  const deleteMutation = useMutation({
    mutationFn: (id: string) => api.deleteAgent(id),
    onSuccess: () => queryClient.invalidateQueries({ queryKey: ['agents'] }),
  });

  if (isLoading) return <div className="text-gray-400">Loading...</div>;

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <h2 className="text-xl font-bold">Agent 管理</h2>
        <button
          onClick={() => setShowCreate(true)}
          className="px-3 py-1.5 bg-indigo-600 hover:bg-indigo-500 text-white text-sm rounded transition-colors"
        >
          + 新建 Agent
        </button>
      </div>

      <div className="space-y-3">
        {agents?.map((a: AgentInfo) => (
          <div key={a.id} className="bg-gray-900 rounded-lg border border-gray-800 p-4">
            <div className="flex items-center justify-between mb-2">
              <div>
                <span className="font-medium">{a.name}</span>
                <span className="text-xs text-gray-500 ml-2">{a.id}</span>
              </div>
              <span className={`text-xs px-2 py-0.5 rounded ${
                a.status === 'running' ? 'bg-green-900/50 text-green-400' :
                a.status === 'error' ? 'bg-red-900/50 text-red-400' : 'bg-gray-800 text-gray-400'
              }`}>{a.status}</span>
            </div>
            <div className="flex items-center gap-2 text-xs text-gray-500 mb-3">
              <span>平台: {a.platform}</span>
              <span>消息: {a.message_count}</span>
              {a.last_error && <span className="text-red-400">错误: {a.last_error}</span>}
            </div>
            <div className="flex gap-2">
              {a.status !== 'running' && (
                <button
                  onClick={() => startMutation.mutate(a.id)}
                  className="px-2 py-1 bg-green-800 hover:bg-green-700 text-green-300 text-xs rounded"
                >
                  启动
                </button>
              )}
              {(a.status === 'running' || a.status === 'starting') && (
                <button
                  onClick={() => stopMutation.mutate(a.id)}
                  className="px-2 py-1 bg-yellow-800 hover:bg-yellow-700 text-yellow-300 text-xs rounded"
                >
                  停止
                </button>
              )}
              {a.status !== 'running' && a.status !== 'starting' && (
                <button
                  onClick={() => { if (confirm('确定删除?')) deleteMutation.mutate(a.id); }}
                  className="px-2 py-1 bg-red-900/50 hover:bg-red-800 text-red-400 text-xs rounded"
                >
                  删除
                </button>
              )}
            </div>
          </div>
        ))}
      </div>

      {showCreate && <CreateAgentModal onClose={() => setShowCreate(false)} />}
    </div>
  );
}

function CreateAgentModal({ onClose }: { onClose: () => void }) {
  const queryClient = useQueryClient();
  const [form, setForm] = useState({ id: '', name: '', platform: 'qq' });

  const createMutation = useMutation({
    mutationFn: (cfg: { id: string; name: string; platform?: string }) => api.createAgent(cfg),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['agents'] });
      onClose();
    },
  });

  return (
    <div className="fixed inset-0 bg-black/60 flex items-center justify-center" onClick={onClose}>
      <div className="bg-gray-900 border border-gray-700 rounded-lg p-6 w-96" onClick={e => e.stopPropagation()}>
        <h3 className="text-lg font-semibold mb-4">新建 Agent</h3>
        <div className="space-y-3">
          <input className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm"
                 placeholder="ID (唯一标识)" value={form.id}
                 onChange={e => setForm({...form, id: e.target.value})} />
          <input className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm"
                 placeholder="名称" value={form.name}
                 onChange={e => setForm({...form, name: e.target.value})} />
          <select className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm"
                  value={form.platform} onChange={e => setForm({...form, platform: e.target.value})}>
            <option value="qq">QQ</option>
          </select>
        </div>
        <div className="flex justify-end gap-2 mt-4">
          <button onClick={onClose} className="px-3 py-1.5 text-sm text-gray-400 hover:text-gray-200">取消</button>
          <button
            onClick={() => createMutation.mutate(form)}
            disabled={!form.id || createMutation.isPending}
            className="px-3 py-1.5 bg-indigo-600 hover:bg-indigo-500 text-white text-sm rounded disabled:opacity-50"
          >
            创建
          </button>
        </div>
        {createMutation.isError && (
          <p className="text-red-400 text-xs mt-2">{(createMutation.error as Error).message}</p>
        )}
      </div>
    </div>
  );
}
