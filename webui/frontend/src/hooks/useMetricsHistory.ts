/**
 * Custom hook: polls api.status() every 2 seconds and accumulates a rolling
 * window of CPU / memory data points.
 *
 * Each point carries an absolute wall-clock timestamp for standard
 * time-series chart display (newest data on the right, old scrolls left).
 */
import { useRef, useState, useEffect, useCallback } from 'react';
import { api } from '../lib/api';

export interface MetricsPoint {
  /** Absolute wall-clock label: "14:32:05". */
  time: string;
  /** Epoch ms for dedup / ordering. */
  timestamp: number;
  cpuPercent: number;
  memoryRssMb: number;
  memoryTotalMb: number;
  /** RSS as percentage of system total (0–100). 0 when total unknown. */
  memoryPercent: number;
}

const POLL_MS = 2000;
const WINDOW_SECS = 120; // show last 2 minutes
const MAX_POINTS = WINDOW_SECS * 1000 / POLL_MS; // ~60

export function useMetricsHistory(maxPoints: number = MAX_POINTS) {
  const [history, setHistory] = useState<MetricsPoint[]>([]);
  const bufferRef = useRef<MetricsPoint[]>([]);

  const fmtTime = useCallback((ts: number) => {
    const d = new Date(ts);
    return d.toLocaleTimeString('zh-CN', {
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
      hour12: false,
    });
  }, []);

  useEffect(() => {
    let active = true;

    const poll = async () => {
      try {
        const status = await api.status();
        if (!active || !status?.system) return;

        const now = Date.now();
        const rss = status.system.memory_rss_mb;
        const total = status.system.memory_total_mb || 0;
        const point: MetricsPoint = {
          time: fmtTime(now),
          timestamp: now,
          cpuPercent: status.system.cpu_percent,
          memoryRssMb: rss,
          memoryTotalMb: total,
          memoryPercent: total > 0 ? (rss / total) * 100 : 0,
        };

        bufferRef.current = [...bufferRef.current, point].slice(-maxPoints);
        setHistory(bufferRef.current);
      } catch {
        // Transient errors are ignored.
      }
    };

    poll();
    const timer = setInterval(poll, POLL_MS);
    return () => {
      active = false;
      clearInterval(timer);
    };
  }, [maxPoints, fmtTime]);

  return history;
}
