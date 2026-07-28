/**
 * Custom hook: polls api.status() every 2 seconds and accumulates a rolling
 * window of CPU / memory data points — both process-level and system-wide.
 *
 * The buffer is pre-seeded with 60 padding slots spanning the last 2 minutes
 * so the X-axis is immediately full-width from the first render.
 */
import { useRef, useState, useEffect, useCallback } from 'react';
import { api } from '../lib/api';

export interface MetricsPoint {
  time: string;
  timestamp: number;

  systemCpuPercent: number;
  cpuPercent: number;

  systemMemoryPercent: number;
  memoryPercent: number;

  systemMemoryUsedMb: number;
  memoryRssMb: number;
  memoryTotalMb: number;
}

const POLL_MS = 2000;
const WINDOW_MS = 120_000; // 2 minutes
const MAX_POINTS = WINDOW_MS / POLL_MS; // 60

function makeTime(ts: number): string {
  const d = new Date(ts);
  return d.toLocaleTimeString('zh-CN', {
    hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false,
  });
}

/** Build a full dummy window so the X axis is 2 min wide from the start. */
function seedBuffer(): MetricsPoint[] {
  const now = Date.now();
  const pts: MetricsPoint[] = [];
  for (let i = 0; i < MAX_POINTS; i++) {
    const t = now - (MAX_POINTS - 1 - i) * POLL_MS;
    pts.push({
      time: makeTime(t),
      timestamp: t,
      systemCpuPercent: 0,
      cpuPercent: 0,
      systemMemoryPercent: 0,
      memoryPercent: 0,
      systemMemoryUsedMb: 0,
      memoryRssMb: 0,
      memoryTotalMb: 0,
    });
  }
  return pts;
}

let g_seeded: MetricsPoint[] | null = null;

export function useMetricsHistory(maxPoints: number = MAX_POINTS) {
  const [history, setHistory] = useState<MetricsPoint[]>(() => {
    if (!g_seeded) g_seeded = seedBuffer();
    return g_seeded;
  });
  const bufferRef = useRef<MetricsPoint[]>(g_seeded!);

  useEffect(() => {
    let active = true;

    const poll = async () => {
      try {
        const status = await api.status();
        if (!active || !status?.system) return;

        const now = Date.now();
        const s = status.system;
        const total = s.memory_total_mb || 0;
        const point: MetricsPoint = {
          time: makeTime(now),
          timestamp: now,
          systemCpuPercent: s.system_cpu_percent ?? 0,
          cpuPercent: s.cpu_percent,
          systemMemoryPercent: total > 0 ? ((s.memory_used_mb ?? 0) / total) * 100 : 0,
          memoryPercent: total > 0 ? (s.memory_rss_mb / total) * 100 : 0,
          systemMemoryUsedMb: s.memory_used_mb ?? 0,
          memoryRssMb: s.memory_rss_mb,
          memoryTotalMb: total,
        };

        bufferRef.current = [...bufferRef.current, point].slice(-maxPoints);
        setHistory(bufferRef.current);
      } catch { /* transient */ }
    };

    poll();
    const timer = setInterval(poll, POLL_MS);
    return () => { active = false; clearInterval(timer); };
  }, [maxPoints]);

  return history;
}
