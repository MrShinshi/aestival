import { useState } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { api, type AgentInfo } from '../lib/api';

export default function Agents() {
  const queryClient = useQueryClient();
  const { data: agents, isLoading } = useQuery({ queryKey: ['agents'], queryFn: api.agents, refetchInterval: 10_000 });
  const [showCreate, setShowCreate] = useState(false);
  const [actionError, setActionError] = useState<string | null>(null);

  const startMutation = useMutation({
    mutationFn: (id: string) => api.agentAction(id, 'start'),
    onSuccess: () => { queryClient.invalidateQueries({ queryKey: ['agents'] }); setActionError(null); },
    onError: (err: Error) => setActionError(`启动失败: ${err.message}`),
  });

  const stopMutation = useMutation({
    mutationFn: (id: string) => api.agentAction(id, 'stop'),
    onSuccess: () => { queryClient.invalidateQueries({ queryKey: ['agents'] }); setActionError(null); },
    onError: (err: Error) => setActionError(`停止失败: ${err.message}`),
  });

  const deleteMutation = useMutation({
    mutationFn: (id: string) => api.deleteAgent(id),
    onSuccess: () => { queryClient.invalidateQueries({ queryKey: ['agents'] }); setActionError(null); },
    onError: (err: Error) => setActionError(`删除失败: ${err.message}`),
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

      {actionError && (
        <div className="bg-red-900/30 border border-red-800 text-red-400 text-sm rounded px-3 py-2 mb-4">
          {actionError}
          <button className="ml-2 text-red-300 hover:text-red-200" onClick={() => setActionError(null)}>✕</button>
        </div>
      )}

      <div className="space-y-3">
        {agents?.map((a: AgentInfo) => (
          <div key={a.id} className="bg-gray-900 rounded-lg border border-gray-800 p-4">
            <div className="flex items-center justify-between mb-2">
              <div className="flex items-center gap-3">
                {a.bot_avatar && (
                  <img src={a.bot_avatar} alt="" className="w-8 h-8 rounded-full" />
                )}
                <div>
                  <div className="font-medium">
                    {a.bot_nick || a.name}
                  </div>
                  <div className="text-xs text-gray-500">{a.id}</div>
                </div>
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
              {a.status !== 'running' && a.status !== 'starting' && (
                <button
                  onClick={() => startMutation.mutate(a.id)}
                  disabled={startMutation.isPending}
                  className="px-2 py-1 bg-green-800 hover:bg-green-700 text-green-300 text-xs rounded disabled:opacity-50"
                >
                  {startMutation.isPending ? '启动中…' : '启动'}
                </button>
              )}
              {(a.status === 'running' || a.status === 'starting') && (
                <button
                  onClick={() => stopMutation.mutate(a.id)}
                  disabled={stopMutation.isPending}
                  className="px-2 py-1 bg-yellow-800 hover:bg-yellow-700 text-yellow-300 text-xs rounded disabled:opacity-50"
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
  const [form, setForm] = useState({
    id: '',
    name: '',
    platform: 'qq',
    qq_app_id: '',
    qq_app_secret: '',
    llm_provider: 'deepseek',
    deepseek_api_key: '',
    deepseek_model: 'deepseek-chat',
    openai_api_key: '',
    openai_model: 'gpt-4o',
  });
  const [localError, setLocalError] = useState('');

  const createMutation = useMutation({
    mutationFn: (cfg: typeof form) => {
      const body: Record<string, unknown> = {
        id: cfg.id,
        name: cfg.name || cfg.id,
        platform: cfg.platform,
        llm_provider: cfg.llm_provider,
      };
      // QQ credentials
      if (cfg.platform === 'qq') {
        body.qq_app_id = cfg.qq_app_id;
        body.qq_app_secret = cfg.qq_app_secret;
      }
      if (cfg.llm_provider === 'deepseek') {
        body.deepseek_api_key = cfg.deepseek_api_key;
        body.deepseek_model = cfg.deepseek_model;
      } else {
        body.openai_api_key = cfg.openai_api_key;
        body.openai_model = cfg.openai_model;
      }
      // api.createAgent accepts a partial config; the Record<string, unknown>
      // conforms to the CreateAgentParams shape at runtime.
      return api.createAgent(body as Parameters<typeof api.createAgent>[0]);
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['agents'] });
      onClose();
    },
  });

  const validate = (): string | null => {
    if (!form.id.trim()) return '请输入 Agent ID';
    if (!/^[a-zA-Z0-9_-]{1,64}$/.test(form.id)) return 'ID 只能包含英文、数字、连字符和下划线（1-64 字符）';
    if (!form.name.trim()) return '请输入名称';
    if (form.platform === 'qq') {
      if (!form.qq_app_id.trim()) return '请输入 QQ Bot App ID';
      if (!form.qq_app_secret.trim()) return '请输入 QQ Bot App Secret';
    }
    if (form.llm_provider === 'deepseek' && !form.deepseek_api_key.trim()) return '请输入 DeepSeek API Key';
    if (form.llm_provider === 'openai' && !form.openai_api_key.trim()) return '请输入 OpenAI API Key';
    return null;
  };

  const handleSubmit = () => {
    const err = validate();
    if (err) { setLocalError(err); return; }
    setLocalError('');
    createMutation.mutate(form);
  };

  const error = localError || (createMutation.isError ? (createMutation.error as Error).message : '');

  return (
    <div className="fixed inset-0 bg-black/60 flex items-center justify-center z-50" onClick={onClose}>
      <div className="bg-gray-900 border border-gray-700 rounded-lg p-6 w-[28rem] max-h-[90vh] overflow-y-auto" onClick={e => e.stopPropagation()}>
        <h3 className="text-lg font-semibold mb-1">创建 Agent</h3>
        <p className="text-xs text-gray-500 mb-4">创建一个新的机器人实例，需要提供 QQ Bot 凭据和 LLM API Key。</p>

        <div className="space-y-3">
          {/* ── 基本信息 ────────────────────────────────── */}
          <div>
            <label className="block text-xs text-gray-400 mb-1">Agent ID <span className="text-red-400">*</span></label>
            <input className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm"
                   placeholder="例如 my-bot-01" value={form.id}
                   onChange={e => setForm({...form, id: e.target.value})} />
            <p className="text-xs text-gray-600 mt-0.5">机器标识，仅支持英文、数字、连字符和下划线</p>
          </div>

          <div>
            <label className="block text-xs text-gray-400 mb-1">名称 <span className="text-red-400">*</span></label>
            <input className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm"
                   placeholder="例如 绯英" value={form.name}
                   onChange={e => setForm({...form, name: e.target.value})} />
            <p className="text-xs text-gray-600 mt-0.5">显示名称，也会作为机器人的默认昵称</p>
          </div>

          <div>
            <label className="block text-xs text-gray-400 mb-1">平台</label>
            <select className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm"
                    value={form.platform} onChange={e => setForm({...form, platform: e.target.value})}>
              <option value="qq">QQ</option>
            </select>
          </div>

          {form.platform === 'qq' && (
            <div className="border-t border-gray-800 pt-3 mt-1 space-y-3">
              <h4 className="text-sm font-medium">QQ Bot 凭据</h4>
              <p className="text-xs text-gray-600">
                在 <a href="https://q.qq.com/qqbot" className="text-indigo-400" target="_blank" rel="noopener">QQ 开放平台</a> 创建机器人后获取 App ID 和 App Secret。
              </p>
              <div>
                <label className="block text-xs text-gray-400 mb-1">App ID <span className="text-red-400">*</span></label>
                <input type="password" className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm"
                       placeholder="QQ Bot App ID" value={form.qq_app_id}
                       onChange={e => setForm({...form, qq_app_id: e.target.value})} />
              </div>
              <div>
                <label className="block text-xs text-gray-400 mb-1">App Secret <span className="text-red-400">*</span></label>
                <input type="password" className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm"
                       placeholder="QQ Bot App Secret" value={form.qq_app_secret}
                       onChange={e => setForm({...form, qq_app_secret: e.target.value})} />
              </div>
            </div>
          )}

          {/* ── LLM 配置 ────────────────────────────────── */}
          <div className="border-t border-gray-800 pt-3 mt-2">
            <h4 className="text-sm font-medium mb-2">LLM 配置</h4>

            <div className="space-y-3">
              <div>
                <label className="block text-xs text-gray-400 mb-1">LLM Provider</label>
                <select className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm"
                        value={form.llm_provider} onChange={e => setForm({...form, llm_provider: e.target.value})}>
                  <option value="deepseek">DeepSeek</option>
                  <option value="openai">OpenAI</option>
                </select>
              </div>

              {form.llm_provider === 'deepseek' ? (
                <>
                  <div>
                    <label className="block text-xs text-gray-400 mb-1">DeepSeek API Key <span className="text-red-400">*</span></label>
                    <input type="password" className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm"
                           placeholder="sk-..." value={form.deepseek_api_key}
                           onChange={e => setForm({...form, deepseek_api_key: e.target.value})} />
                  </div>
                  <div>
                    <label className="block text-xs text-gray-400 mb-1">Model</label>
                    <input className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm"
                           placeholder="deepseek-chat" value={form.deepseek_model}
                           onChange={e => setForm({...form, deepseek_model: e.target.value})} />
                  </div>
                </>
              ) : (
                <>
                  <div>
                    <label className="block text-xs text-gray-400 mb-1">OpenAI API Key <span className="text-red-400">*</span></label>
                    <input type="password" className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm"
                           placeholder="sk-..." value={form.openai_api_key}
                           onChange={e => setForm({...form, openai_api_key: e.target.value})} />
                  </div>
                  <div>
                    <label className="block text-xs text-gray-400 mb-1">Model</label>
                    <input className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm"
                           placeholder="gpt-4o" value={form.openai_model}
                           onChange={e => setForm({...form, openai_model: e.target.value})} />
                  </div>
                </>
              )}
            </div>
          </div>

          {/* ── 说明 ──────────────────────────────────── */}
          <p className="text-xs text-gray-600 bg-gray-800/50 rounded p-2">
            API Key 由您自己提供，本系统不负责提供免费算力。
            DeepSeek 可在 <a href="https://platform.deepseek.com" className="text-indigo-400" target="_blank" rel="noopener">platform.deepseek.com</a> 获取，
            OpenAI 可在 <a href="https://platform.openai.com" className="text-indigo-400" target="_blank" rel="noopener">platform.openai.com</a> 获取。
          </p>
        </div>

        <div className="flex justify-end gap-2 mt-4">
          <button onClick={onClose} className="px-3 py-1.5 text-sm text-gray-400 hover:text-gray-200">取消</button>
          <button
            onClick={handleSubmit}
            disabled={createMutation.isPending}
            className="px-3 py-1.5 bg-indigo-600 hover:bg-indigo-500 text-white text-sm rounded disabled:opacity-50"
          >
            {createMutation.isPending ? '创建中…' : '创建 Agent'}
          </button>
        </div>
        {error && (
          <p className="text-red-400 text-xs mt-2">{error}</p>
        )}
      </div>
    </div>
  );
}
