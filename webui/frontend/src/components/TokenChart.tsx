import { useMemo } from 'react';
import {
  AreaChart, Area, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, Legend,
} from 'recharts';
import type { TokenStat } from '../lib/api';

interface TokenChartProps {
  data: TokenStat[];
  days?: number; // number of recent days to show
}

export default function TokenChart({ data, days = 30 }: TokenChartProps) {
  const sliced = useMemo(() => {
    if (!data || data.length === 0) return [];
    return data.slice(-days);
  }, [data, days]);

  if (sliced.length === 0) {
    return (
      <div className="text-gray-500 text-sm p-4 text-center">
        暂无 Token 用量数据
      </div>
    );
  }

  return (
    <ResponsiveContainer width="100%" height={280}>
      <AreaChart data={sliced} margin={{ top: 8, right: 8, left: -16, bottom: 0 }}>
        <defs>
          <linearGradient id="promptGrad" x1="0" y1="0" x2="0" y2="1">
            <stop offset="5%" stopColor="#818cf8" stopOpacity={0.3} />
            <stop offset="95%" stopColor="#818cf8" stopOpacity={0} />
          </linearGradient>
          <linearGradient id="completionGrad" x1="0" y1="0" x2="0" y2="1">
            <stop offset="5%" stopColor="#4ade80" stopOpacity={0.3} />
            <stop offset="95%" stopColor="#4ade80" stopOpacity={0} />
          </linearGradient>
        </defs>
        <CartesianGrid strokeDasharray="3 3" stroke="#374151" />
        <XAxis
          dataKey="date"
          tick={{ fill: '#9ca3af', fontSize: 11 }}
          tickFormatter={(v: string) => {
            const parts = v.split('-');
            return `${parts[1] || ''}/${parts[2] || ''}`;
          }}
          interval="preserveStartEnd"
        />
        <YAxis tick={{ fill: '#9ca3af', fontSize: 11 }} width={60} />
        <Tooltip
          contentStyle={{
            backgroundColor: '#1f2937',
            border: '1px solid #374151',
            borderRadius: '6px',
            fontSize: '12px',
          }}
          labelStyle={{ color: '#9ca3af' }}
          formatter={(value) => (typeof value === 'number' ? value.toLocaleString() : String(value))}
        />
        <Legend
          wrapperStyle={{ fontSize: '12px', color: '#9ca3af' }}
        />
        <Area
          type="monotone"
          dataKey="prompt_tokens"
          name="Prompt Tokens"
          stroke="#818cf8"
          fill="url(#promptGrad)"
          strokeWidth={2}
        />
        <Area
          type="monotone"
          dataKey="completion_tokens"
          name="Completion Tokens"
          stroke="#4ade80"
          fill="url(#completionGrad)"
          strokeWidth={2}
        />
      </AreaChart>
    </ResponsiveContainer>
  );
}
