import { useState, useMemo } from 'react';
import { useQuery } from '@tanstack/react-query';
import { ArrowLeft } from 'lucide-react';
import { api, type ConversationSummary } from '../lib/api';

export default function Conversations() {
  const { data, isLoading } = useQuery({
    queryKey: ['conversations'],
    queryFn: () => api.conversations(200),
    refetchInterval: 30_000,
  });

  // Cross-reference agents to resolve agent_id → display name
  const { data: agents } = useQuery({
    queryKey: ['agents'],
    queryFn: api.agents,
    refetchInterval: 30_000,
  });

  const agentNames = useMemo(() => {
    if (!agents) return {} as Record<string, string>;
    const m: Record<string, string> = {};
    for (const a of agents) {
      m[a.id] = a.bot_nick || a.name || a.id;
    }
    return m;
  }, [agents]);

  const [selected, setSelected] = useState<{ convoId: string; agentId: string } | null>(null);

  // Group by agent → convo_type → conversation
  const groups = useMemo(() => {
    if (!data?.conversations) return [];
    const map = new Map<string, Map<string, ConversationSummary[]>>();
    for (const c of data.conversations) {
      const a = c.agent_id || 'default';
      const t = c.convo_type || '其他';
      if (!map.has(a)) map.set(a, new Map());
      const tm = map.get(a)!;
      if (!tm.has(t)) tm.set(t, []);
      tm.get(t)!.push(c);
    }
    return Array.from(map.entries()).map(([agent, typeMap]) => ({
      agent,
      types: Array.from(typeMap.entries()).map(([type, convos]) => ({ type, convos })),
    }));
  }, [data]);

  return (
    <div>
      <h2 className="text-xl font-bold mb-6">对话审查</h2>

      <div className="md:flex md:gap-4">
        {/* List — full width on mobile, fixed-width column on md+ */}
        <div className={`space-y-4 md:w-96 md:flex-shrink-0 md:max-h-[calc(100dvh-200px)] md:overflow-auto ${selected ? 'hidden md:block' : ''}`}>
          {groups.map(g => (
            <div key={g.agent}>
              <div className="text-xs font-semibold text-indigo-400 mb-1 uppercase tracking-wider">
                {agentNames[g.agent] || g.agent}
              </div>
              {g.types.map(gt => (
                <div key={gt.type} className="mb-2">
                  <div className="text-xs text-gray-500 ml-1 mb-1">{gt.type}</div>
                  <div className="space-y-0.5">
                    {gt.convos.map(c => (
                      <button
                        key={`${c.agent_id}:${c.convo_id}`}
                        onClick={() => setSelected({ convoId: c.convo_id, agentId: c.agent_id })}
                        className={`w-full text-left px-3 py-2 rounded text-sm transition-colors ${
                          selected?.convoId === c.convo_id
                            ? 'bg-indigo-900/40 border border-indigo-800'
                            : 'bg-gray-900/60 border border-gray-800/50 hover:bg-gray-800'
                        }`}
                      >
                        <div className="text-gray-200 truncate text-xs">
                          {c.title || c.convo_id}
                        </div>
                        <div className="flex justify-between mt-0.5 text-xs text-gray-600">
                          <span>{c.message_count} 条</span>
                          <span>{c.last_at?.slice(0, 10)}</span>
                        </div>
                      </button>
                    ))}
                  </div>
                </div>
              ))}
            </div>
          ))}
          {(!data?.conversations || data.conversations.length === 0) && (
            <div className="text-gray-500 text-sm py-4 text-center">暂无对话</div>
          )}
        </div>

        {/* Detail — hidden on mobile until a conversation is selected */}
        <div className={`bg-gray-900 rounded-lg border border-gray-800 p-4 md:flex-1 md:max-h-[calc(100dvh-200px)] md:overflow-auto ${selected ? '' : 'hidden md:block'}`}>
          {selected ? (
            <>
              {/* Mobile back button */}
              <button
                onClick={() => setSelected(null)}
                className="md:hidden flex items-center gap-1 text-sm text-gray-400 hover:text-gray-200 mb-3"
              >
                <ArrowLeft size={16} aria-hidden="true" />
                返回列表
              </button>
              <ConversationDetail convoId={selected.convoId} agentId={selected.agentId} />
            </>
          ) : (
            <div className="text-gray-500 text-sm py-8 text-center">选择左侧对话查看详情</div>
          )}
        </div>
      </div>
    </div>
  );
}

function ConversationDetail({ convoId, agentId }: { convoId: string; agentId?: string }) {
  const { data, isLoading } = useQuery({
    queryKey: ['conversation', convoId, agentId],
    queryFn: () => api.conversation(convoId, agentId),
  });

  // Resolve agent name from the agents list
  const { data: agents } = useQuery({
    queryKey: ['agents'],
    queryFn: api.agents,
    staleTime: 60_000,
  });
  const agentName = useMemo(() => {
    if (!agents || !data) return data?.agent_id || 'default';
    const a = agents.find(x => x.id === data.agent_id);
    return a ? (a.bot_nick || a.name || a.id) : (data.agent_id || 'default');
  }, [agents, data]);

  if (isLoading) return <div className="text-gray-400 text-sm">Loading...</div>;
  if (!data) return <div className="text-gray-400 text-sm">Not found</div>;

  return (
    <div className="space-y-3">
      <div className="text-sm text-gray-500 mb-2">
        <span className="text-indigo-400">{agentName}</span>
        {' › '}
        <span>{data.convo_type}</span>
        {' › '}
        <span className="text-gray-300">{data.title}</span>
        <span className="text-gray-600 ml-2 text-xs">{data.messages.length} 条消息</span>
      </div>
      {data.messages.map((msg, i) => (
        <div key={i} className={`p-3 rounded-lg text-sm ${
          msg.role === 'user' ? 'bg-gray-800 ml-0 mr-8' :
          msg.role === 'assistant' ? 'bg-indigo-950/30 ml-8 mr-0' :
          msg.role === 'system' ? 'bg-gray-900 text-gray-500 mx-4 text-xs' :
          'bg-gray-900 text-yellow-400 mx-4 text-xs'
        }`}>
          <div className="text-xs text-gray-500 mb-1 flex gap-2">
            <span>{msg.role}</span>
            {msg.nick && <span className="text-indigo-400">{msg.nick}</span>}
            <span className="ml-auto">{msg.created_at?.slice(0, 19).replace('T', ' ')}</span>
          </div>
          <div className="whitespace-pre-wrap break-words">{msg.content}</div>
        </div>
      ))}
    </div>
  );
}
