/**
 * Real-time resource line chart — wall-clock X axis, scrolling right.
 * All series share one Y axis.  Only newly appended data points
 * enter visually; existing points stay put (no re-animation).
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
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type ChartDataPoint = Record<string, any>;

interface ResourceChartProps {
  data: ChartDataPoint[];
  series: SeriesConfig[];
  height?: number;
  showLegend?: boolean;
}

function niceCeil(val: number): number {
  if (val <= 0) return 5;
  if (val <= 1) return 2;
  if (val <= 5) return 5;
  const step = val <= 20 ? 5 : 10;
  return Math.min(Math.ceil(val / step) * step, 100);
}

export default function ResourceChart({ data, series, height = 200, showLegend = false }: ResourceChartProps) {
  const tickInterval = useMemo(() => {
    if (data.length <= 6) return 0;
    return Math.max(1, Math.floor(data.length / 6));
  }, [data]);

  // Shared Y-axis domain — covers the max across all series.
  const yDomain = useMemo((): [number, number] => {
    let maxVal = 0;
    for (const pt of data) {
      for (const s of series) {
        const v = pt[s.dataKey];
        if (v != null && typeof v === 'number' && v > maxVal) maxVal = v;
      }
    }
    return [0, niceCeil(maxVal * 1.15)];
  }, [data, series]);

  return (
    <ResponsiveContainer width="100%" height={height}>
      <LineChart data={data} margin={{ top: 8, right: 8, left: -8, bottom: 0 }}>
        <CartesianGrid strokeDasharray="3 3" stroke="#374151" />
        <XAxis
          dataKey="time"
          tick={{ fill: '#9ca3af', fontSize: 10 }}
          interval={tickInterval}
        />
        <YAxis
          tick={{ fill: '#9ca3af', fontSize: 10 }}
          domain={yDomain}
          unit="%"
          width={44}
        />
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
            return [`${num.toFixed(1)}%`, typeof name === 'string' ? name : ''];
          }}
        />
        {showLegend && series.length > 1 && (
          <Legend wrapperStyle={{ fontSize: '11px', color: '#9ca3af' }} />
        )}
        {series.map(s => (
          <Line
            key={s.dataKey}
            type="monotone"
            dataKey={s.dataKey}
            name={s.name}
            stroke={s.color}
            strokeWidth={2}
            dot={false}
            activeDot={{ r: 3, fill: s.color }}
            isAnimationActive={false}
          />
        ))}
      </LineChart>
    </ResponsiveContainer>
  );
}
