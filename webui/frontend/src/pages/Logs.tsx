import { useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { api } from '../lib/api';

const levelBadges: Record<string, { color: string; badge: string }> = {
  ERROR: { color: 'text-red-400', badge: 'bg-red-900/50 text-red-400' },
  WARN:  { color: 'text-yellow-400', badge: 'bg-yellow-900/50 text-yellow-400' },
  INFO:  { color: 'text-blue-400', badge: 'bg-blue-900/50 text-blue-400' },
  DEBUG: { color: 'text-gray-500', badge: 'bg-gray-800 text-gray-500' },
};

export default function Logs() {
  const [level, setLevel] = useState('');
  const [limit, setLimit] = useState(100);

  const { data, isLoading, refetch } = useQuery({
    queryKey: ['logs', level, limit],
    queryFn: () => api.logs(level || undefined, limit),
    refetchInterval: 5_000,
  });

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <h2 className="text-xl font-bold">日志</h2>
        <div className="flex items-center gap-2">
          <select className="bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm"
                  value={level} onChange={e => setLevel(e.target.value)}>
            <option value="">全部级别</option>
            <option value="error">ERROR</option>
            <option value="warn">WARN</option>
            <option value="info">INFO</option>
            <option value="debug">DEBUG</option>
          </select>
          <button onClick={() => refetch()}
                  className="px-2 py-1 bg-gray-800 hover:bg-gray-700 text-sm rounded border border-gray-700">
            刷新
          </button>
        </div>
      </div>

      {isLoading ? (
        <div className="text-gray-400">Loading...</div>
      ) : (
        <div className="bg-gray-900 rounded-lg border border-gray-800 p-4 font-mono text-xs leading-6 max-h-[calc(100vh-200px)] overflow-auto">
          {data?.lines.map((line, i) => {
            const lvl = ['ERROR', 'WARN', 'INFO', 'DEBUG'].find(l => line.includes(`[${l}]`));
            const info = lvl ? levelBadges[lvl] : { color: 'text-gray-400', badge: '' };
            return (
              <div key={i} className={info.color} role="log" aria-label={lvl ? `level: ${lvl}` : undefined}>
                {lvl && <span className={`inline-block text-[10px] px-1 rounded mr-1 align-middle ${info.badge}`}>{lvl}</span>}
                {line}
              </div>
            );
          })}
          {(!data?.lines || data.lines.length === 0) && (
            <div className="text-gray-500 py-4 text-center">无日志</div>
          )}
        </div>
      )}

      <div className="text-xs text-gray-500 mt-2">
        {data?.returned || 0} / {data?.total || 0} 条
      </div>
    </div>
  );
}
