/**
 * Custom hook: polls api.status() every 2 seconds and accumulates a rolling
 * window of CPU / memory data points for the real-time line chart.
 *
 * 2-second polling + smooth line animation mimics the Windows Task Manager
 * Performance tab feel.
 */
import { useRef, useState, useEffect } from 'react';
import { api } from '../lib/api';

export interface MetricsPoint {
  time: string;       // HH:MM:SS label for the X axis
  timestamp: number;  // epoch ms (for dedup / ordering)
  cpuPercent: number;
  memoryRssMb: number;
  memoryTotalMb: number;
  memoryPercent: number; // RSS / total * 100, 0 when total is unknown
}

const DEFAULT_MAX_POINTS = 60;  // 2 minutes at 2-second intervals

export function useMetricsHistory(maxPoints: number = DEFAULT_MAX_POINTS) {
  const [history, setHistory] = useState<MetricsPoint[]>([]);
  const bufferRef = useRef<MetricsPoint[]>([]);

  useEffect(() => {
    let active = true;

    const poll = async () => {
      try {
        const status = await api.status();
        if (!active || !status?.system) return;

        const now = new Date();
        const total = status.system.memory_total_mb || 0;
        const point: MetricsPoint = {
          time: now.toLocaleTimeString('zh-CN', {
            hour: '2-digit',
            minute: '2-digit',
            second: '2-digit',
            hour12: false,
          }),
          timestamp: now.getTime(),
          cpuPercent: status.system.cpu_percent,
          memoryRssMb: status.system.memory_rss_mb,
          memoryTotalMb: total,
          memoryPercent: total > 0 ? (status.system.memory_rss_mb / total) * 100 : 0,
        };

        bufferRef.current = [...bufferRef.current, point].slice(-maxPoints);
        setHistory(bufferRef.current);
      } catch {
        // Ignore transient poll errors so the chart keeps the last-known data.
      }
    };

    poll(); // immediate first sample
    const interval = setInterval(poll, 2000);

    return () => {
      active = false;
      clearInterval(interval);
    };
  }, [maxPoints]);

  return history;
}
