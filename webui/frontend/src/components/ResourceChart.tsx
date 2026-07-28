/**
 * Generic real-time line chart — wall-clock X axis, scrolling right.
 * Supports multiple series on independent Y axes.
 *
 * When asPercent is true, the Y axis auto-scales from 0 up to the
 * nearest round number that fits the data (≤ 100).
 */
import { useMemo } from 'react';
import {
  LineChart, Line, XAxis, YAxis, CartesianGrid,
  Tooltip, ResponsiveContainer, Legend,
} from 'recharts';

export interface SeriesConfig {
  dataKey: string;
  name: string;
  color: string;
  asPercent?: boolean;
  /** Override auto domain. Percent series default to dynamic [0, N≤100]. */
  domain?: [number, number];
}

interface ResourceChartProps {
  data: any[];
  series: SeriesConfig[];
  height?: number;
  /** Show legend only when there are ≥ 2 lines. */
  showLegend?: boolean;
}

function fmtMB(mb: number): string {
  if (mb >= 1024) return `${(mb / 1024).toFixed(1)} GB`;
  return `${mb.toFixed(0)} MB`;
}

/** Snap a value up to a "nice" round number. */
function niceCeil(val: number, max: number): number {
  if (val <= 0) return 5;
  if (val <= 1) return 1;
  if (val <= 5) return 5;
  // next multiple of 5 or 10
  const step = val <= 20 ? 5 : 10;
  const ceil = Math.ceil(val / step) * step;
  return Math.min(ceil, max);
}

export default function ResourceChart({ data, series, height = 200, showLegend = false }: ResourceChartProps) {
  const tickInterval = useMemo(() => {
    if (data.length <= 6) return 0;
    return Math.max(1, Math.floor(data.length / 6));
  }, [data.length]);

  // Compute dynamic Y-axis domains for percent series.
  const domains = useMemo(() => {
    const map: Record<string, [number, number]> = {};
    if (data.length === 0) return map;
    for (const s of series) {
      if (s.domain) {
        map[s.dataKey] = s.domain;
        continue;
      }
      if (!s.asPercent) {
        map[s.dataKey] = [0, 'auto' as any];
        continue;
      }
      // Collect all values for this key across the visible window.
      let maxVal = 0;
      for (const pt of data) {
        const v = pt[s.dataKey];
        if (typeof v === 'number' && v > maxVal) maxVal = v;
      }
      const ceil = niceCeil(maxVal * 1.15, 100);
      map[s.dataKey] = [0, ceil];
    }
    return map;
  }, [data, series]);

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
            domain={domains[s.dataKey] || [0, 'auto']}
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
        {showLegend && series.length > 1 && (
          <Legend wrapperStyle={{ fontSize: '11px', color: '#9ca3af' }} />
        )}
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
