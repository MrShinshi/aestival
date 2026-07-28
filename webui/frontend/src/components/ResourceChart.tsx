/**
 * Real-time resource line chart — renders CPU % and Memory usage as dual-axis
 * dynamic lines with a sliding time window.
 *
 * Styled to feel like Windows Task Manager's Performance tab: smooth
 * animation on every new data point, subtle gradient fill, clean grid.
 *
 * Uses recharts (already in the project) with the same dark-theme styling as
 * TokenChart so the dashboard reads as one consistent system.
 */
import { useMemo } from 'react';
import {
  LineChart, Line, XAxis, YAxis, CartesianGrid,
  Tooltip, ResponsiveContainer, Legend,
} from 'recharts';
import type { MetricsPoint } from '../hooks/useMetricsHistory';

interface ResourceChartProps {
  data: MetricsPoint[];
  height?: number;
}

function fmtMB(mb: number): string {
  if (mb >= 1024) return `${(mb / 1024).toFixed(1)} GB`;
  return `${mb.toFixed(0)} MB`;
}

export default function ResourceChart({ data, height = 200 }: ResourceChartProps) {
  // Compute memory Y-axis domain so the line doesn't hug the baseline.
  const memDomain = useMemo(() => {
    if (data.length === 0) return [0, 100] as [number, number];
    const values = data.map(d => d.memoryRssMb);
    const min = Math.min(...values);
    const max = Math.max(...values);
    const pad = Math.max((max - min) * 0.2, 50);
    return [
      Math.max(0, Math.floor(min - pad)),
      Math.ceil(max + pad),
    ] as [number, number];
  }, [data]);

  // Show sparser X-axis ticks — every ~10 seconds (every 5th point at 2s intervals).
  const tickInterval = useMemo(() => {
    if (data.length <= 10) return 0; // show all when there's little data
    return Math.max(1, Math.floor(data.length / 8));
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
        <defs>
          <linearGradient id="cpuLineGrad" x1="0" y1="0" x2="0" y2="1">
            <stop offset="5%" stopColor="#818cf8" stopOpacity={0.25} />
            <stop offset="95%" stopColor="#818cf8" stopOpacity={0} />
          </linearGradient>
          <linearGradient id="memLineGrad" x1="0" y1="0" x2="0" y2="1">
            <stop offset="5%" stopColor="#4ade80" stopOpacity={0.2} />
            <stop offset="95%" stopColor="#4ade80" stopOpacity={0} />
          </linearGradient>
        </defs>
        <CartesianGrid strokeDasharray="3 3" stroke="#374151" />
        <XAxis
          dataKey="time"
          tick={{ fill: '#9ca3af', fontSize: 10 }}
          interval={tickInterval}
        />
        {/* CPU axis — left, fixed 0–100 % */}
        <YAxis
          yAxisId="cpu"
          orientation="left"
          tick={{ fill: '#818cf8', fontSize: 10 }}
          domain={[0, 100]}
          unit="%"
          width={42}
        />
        {/* Memory axis — right, auto-scaled with padding */}
        <YAxis
          yAxisId="mem"
          orientation="right"
          tick={{ fill: '#4ade80', fontSize: 10 }}
          domain={memDomain}
          tickFormatter={(v: number) => (v >= 1024 ? `${(v / 1024).toFixed(1)}G` : `${v.toFixed(0)}M`)}
          width={50}
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
            if (name === 'CPU %') return [`${num.toFixed(1)}%`, name];
            return [fmtMB(num), name];
          }}
        />
        <Legend
          wrapperStyle={{ fontSize: '12px', color: '#9ca3af' }}
        />
        {/* CPU line with smooth Task-Manager-style animation */}
        <Line
          yAxisId="cpu"
          type="monotone"
          dataKey="cpuPercent"
          name="CPU %"
          stroke="#818cf8"
          strokeWidth={2}
          dot={false}
          activeDot={{ r: 3, fill: '#818cf8' }}
          isAnimationActive={true}
          animationDuration={400}
          animationEasing="ease-out"
        />
        {/* Memory line */}
        <Line
          yAxisId="mem"
          type="monotone"
          dataKey="memoryRssMb"
          name="内存"
          stroke="#4ade80"
          strokeWidth={2}
          dot={false}
          activeDot={{ r: 3, fill: '#4ade80' }}
          isAnimationActive={true}
          animationDuration={400}
          animationEasing="ease-out"
        />
      </LineChart>
    </ResponsiveContainer>
  );
}
