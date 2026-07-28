/**
 * Custom hook: polls api.status() every 2 seconds and accumulates a rolling
 * window of CPU / memory data points — both process-level and system-wide.
 */
import { useRef, useState, useEffect, useCallback } from 'react';
import { api } from '../lib/api';

export interface MetricsPoint {
  time: string;
  timestamp: number;

  /** System-wide CPU 0–100. */
  systemCpuPercent: number;
  /** Process CPU 0–100 (per-core normalised). */
  cpuPercent: number;

  /** System-wide memory used % (0–100). */
  systemMemoryPercent: number;
  /** Process RSS as % of total RAM (0–100). */
  memoryPercent: number;

  /** System-wide used RAM in MB. */
  systemMemoryUsedMb: number;
  /** Process RSS in MB. */
  memoryRssMb: number;
  memoryTotalMb: number;
}

const POLL_MS = 2000;
const WINDOW_SECS = 120;
const MAX_POINTS = WINDOW_SECS * 1000 / POLL_MS; // ~60

export function useMetricsHistory(maxPoints: number = MAX_POINTS) {
  const [history, setHistory] = useState<MetricsPoint[]>([]);
  const bufferRef = useRef<MetricsPoint[]>([]);

  const fmtTime = useCallback((ts: number) => {
    const d = new Date(ts);
    return d.toLocaleTimeString('zh-CN', {
      hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false,
    });
  }, []);

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
          time: fmtTime(now),
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
  }, [maxPoints, fmtTime]);

  return history;
}
