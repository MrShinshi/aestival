import { useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { api, type ConversationSummary, type ConversationDetail } from '../lib/api';

export default function Conversations() {
  const { data, isLoading } = useQuery({
    queryKey: ['conversations'],
    queryFn: () => api.conversations(50),
    refetchInterval: 30_000,
  });

  const [selected, setSelected] = useState<string | null>(null);

  return (
    <div>
      <h2 className="text-xl font-bold mb-6">对话审查</h2>

      <div className="flex gap-4">
        {/* List */}
        <div className="w-80 flex-shrink-0 space-y-1 max-h-[calc(100vh-200px)] overflow-auto">
          {data?.conversations.map((c: ConversationSummary) => (
            <button
              key={c.convo_id}
              onClick={() => setSelected(c.convo_id)}
              className={`w-full text-left p-3 rounded-lg text-sm transition-colors ${
                selected === c.convo_id
                  ? 'bg-indigo-900/40 border border-indigo-800'
                  : 'bg-gray-900 border border-gray-800 hover:bg-gray-800'
              }`}
            >
              <div className="font-mono text-xs text-gray-400 truncate">{c.convo_id}</div>
              <div className="flex justify-between mt-1 text-xs text-gray-500">
                <span>{c.message_count} 条消息</span>
                <span>{c.last_at?.slice(0, 10)}</span>
              </div>
            </button>
          ))}
          {(!data?.conversations || data.conversations.length === 0) && (
            <div className="text-gray-500 text-sm py-4 text-center">暂无对话</div>
          )}
        </div>

        {/* Detail */}
        <div className="flex-1 bg-gray-900 rounded-lg border border-gray-800 p-4 max-h-[calc(100vh-200px)] overflow-auto">
          {selected ? (
            <ConversationDetail convoId={selected} />
          ) : (
            <div className="text-gray-500 text-sm py-8 text-center">选择左侧对话查看详情</div>
          )}
        </div>
      </div>
    </div>
  );
}

function ConversationDetail({ convoId }: { convoId: string }) {
  const { data, isLoading } = useQuery({
    queryKey: ['conversation', convoId],
    queryFn: () => api.conversation(convoId),
  });

  if (isLoading) return <div className="text-gray-400 text-sm">Loading...</div>;
  if (!data) return <div className="text-gray-400 text-sm">Not found</div>;

  return (
    <div className="space-y-3">
      {data.messages.map((msg, i) => (
        <div key={i} className={`p-3 rounded-lg text-sm ${
          msg.role === 'user' ? 'bg-gray-800 ml-0 mr-8' :
          msg.role === 'assistant' ? 'bg-indigo-950/30 ml-8 mr-0' :
          msg.role === 'system' ? 'bg-gray-900 text-gray-500 mx-4 text-xs' :
          'bg-gray-900 text-yellow-400 mx-4 text-xs'
        }`}>
          <div className="text-xs text-gray-500 mb-1">
            {msg.role} {msg.nick && `(${msg.nick})`} · {msg.created_at?.slice(0, 19).replace('T', ' ')}
          </div>
          <div className="whitespace-pre-wrap break-words">{msg.content}</div>
        </div>
      ))}
    </div>
  );
}
