/**
 * Custom hook: polls api.status() every 2 seconds and accumulates a rolling
 * window of CPU / memory data points for the real-time line chart.
 *
 * The latest N points are kept; each point carries a relative second offset
 * (seconds ago) so the chart X-axis can show "2:00 ... 0:00" Task-Manager-style.
 */
import { useRef, useState, useEffect, useCallback } from 'react';
import { api } from '../lib/api';

export interface MetricsPoint {
  /** Relative seconds ago (0 = now, positive = older). */
  secondsAgo: number;
  /** Formatted label like "1:30" or "0:05". */
  timeLabel: string;
  cpuPercent: number;
  memoryRssMb: number;
  memoryTotalMb: number;
}

const DEFAULT_MAX_POINTS = 60;  // 2 minutes at 2-second intervals
const POLL_MS = 2000;

export function useMetricsHistory(maxPoints: number = DEFAULT_MAX_POINTS) {
  const [history, setHistory] = useState<MetricsPoint[]>([]);
  // Keep a mutable ref because the "seconds ago" field shifts every tick.
  const bufferRef = useRef<{ ts: number; cpu: number; rss: number; total: number }[]>([]);

  const rebuild = useCallback((raw: typeof bufferRef.current) => {
    const now = Date.now();
    const points: MetricsPoint[] = raw.map(r => {
      const ago = Math.round((now - r.ts) / 1000);
      const m = Math.floor(ago / 60);
      const s = ago % 60;
      return {
        secondsAgo: ago,
        timeLabel: `${m}:${String(s).padStart(2, '0')}`,
        cpuPercent: r.cpu,
        memoryRssMb: r.rss,
        memoryTotalMb: r.total,
      };
    });
    setHistory(points);
  }, []);

  useEffect(() => {
    let active = true;
    let ticker: ReturnType<typeof setInterval> | undefined;

    const poll = async () => {
      try {
        const status = await api.status();
        if (!active || !status?.system) return;

        const raw = bufferRef.current;
        raw.push({
          ts: Date.now(),
          cpu: status.system.cpu_percent,
          rss: status.system.memory_rss_mb,
          total: status.system.memory_total_mb || 0,
        });
        if (raw.length > maxPoints) raw.splice(0, raw.length - maxPoints);

        rebuild(raw);
      } catch {
        // Transient errors are ignored; chart keeps the last known window.
      }
    };

    poll();
    const pollTimer = setInterval(poll, POLL_MS);

    // Also re-label every 2 s so the "seconds ago" labels stay accurate
    // without needing a new data point — important when the process is idle
    // and CPU/memory barely change.
    ticker = setInterval(() => rebuild(bufferRef.current), 2000);

    return () => {
      active = false;
      clearInterval(pollTimer);
      if (ticker) clearInterval(ticker);
    };
  }, [maxPoints, rebuild]);

  return history;
}
