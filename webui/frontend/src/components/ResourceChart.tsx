/**
 * Generic real-time line chart — wall-clock X axis, scrolling right.
 * Used for CPU % and Memory % — same visual language as TokenChart.
 */
import { useMemo } from 'react';
import {
  LineChart, Line, XAxis, YAxis, CartesianGrid,
  Tooltip, ResponsiveContainer,
} from 'recharts';

export interface SeriesConfig {
  dataKey: string;
  name: string;
  color: string;
  /** true → format as "12.3%", false → format as "1.2 GB" */
  asPercent?: boolean;
  /** Fixed Y-axis domain. */
  domain?: [number, number];
}

interface ResourceChartProps {
  data: any[];
  series: SeriesConfig[];
  height?: number;
}

function fmtMB(mb: number): string {
  if (mb >= 1024) return `${(mb / 1024).toFixed(1)} GB`;
  return `${mb.toFixed(0)} MB`;
}

export default function ResourceChart({ data, series, height = 200 }: ResourceChartProps) {
  const tickInterval = useMemo(() => {
    if (data.length <= 6) return 0;
    return Math.max(1, Math.floor(data.length / 6));
  }, [data.length]);

  if (data.length === 0) {
    return (
      <div className="text-gray-500 text-sm p-4 text-center">
        正在采集系统资源数据…
      </div>
    );
  }

  return (
    <ResponsiveContainer width="100%" height={height}>
      <LineChart data={data} margin={{ top: 8, right: 8, left: -8, bottom: 0 }}>
        <CartesianGrid strokeDasharray="3 3" stroke="#374151" />
        <XAxis
          dataKey="time"
          tick={{ fill: '#9ca3af', fontSize: 10 }}
          interval={tickInterval}
        />
        {series.map(s => (
          <YAxis
            key={`y-${s.dataKey}`}
            yAxisId={s.dataKey}
            orientation={s.dataKey === series[0].dataKey ? 'left' : 'right'}
            tick={{ fill: s.color, fontSize: 10 }}
            domain={s.domain || [0, 'auto']}
            unit={s.asPercent ? '%' : undefined}
            tickFormatter={s.asPercent ? undefined : (v => fmtMB(v as number))}
            width={48}
          />
        ))}
        <Tooltip
          contentStyle={{
            backgroundColor: '#1f2937',
            border: '1px solid #374151',
            borderRadius: '6px',
            fontSize: '12px',
          }}
          labelStyle={{ color: '#9ca3af' }}
          formatter={(value, name) => {
            const num = typeof value === 'number' ? value : 0;
            const label = typeof name === 'string' ? name : '';
            const cfg = series.find(s => s.name === label);
            if (cfg?.asPercent) return [`${num.toFixed(1)}%`, label];
            return [fmtMB(num), label];
          }}
        />
        {series.map(s => (
          <Line
            key={s.dataKey}
            yAxisId={s.dataKey}
            type="monotone"
            dataKey={s.dataKey}
            name={s.name}
            stroke={s.color}
            strokeWidth={2}
            dot={false}
            activeDot={{ r: 3, fill: s.color }}
            isAnimationActive={true}
            animationDuration={400}
            animationEasing="ease-out"
          />
        ))}
      </LineChart>
    </ResponsiveContainer>
  );
}
