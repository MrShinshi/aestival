/**
 * Custom hook: loads cached 2-minute metrics history on mount, then polls
 * /api/ui/status every second for incremental updates.
 *
 * The backend keeps a warm ring buffer (1 s resolution, 2 min window) via a
 * background poller.  History snapshots carry server-side timestamps (_ts)
 * so they are inserted at their true position — no pre-allocated seed slots,
 * no clock-skew rejection.  The chart grows naturally from existing data
 * and stays gap-free.
 */
import { useRef, useState, useEffect } from 'react';
import { api } from '../lib/api';

export interface MetricsPoint {
  time: string;
  timestamp: number;

  systemCpuPercent: number | null;
  cpuPercent: number | null;

  systemMemoryPercent: number | null;
  memoryPercent: number | null;

  systemMemoryUsedMb: number | null;
  memoryRssMb: number | null;
  memoryTotalMb: number | null;
}

const POLL_MS = 1000;
const MAX_POINTS = 120; // 2 minutes at 1-second resolution

function makeTime(ts: number): string {
  const d = new Date(ts);
  return d.toLocaleTimeString('zh-CN', {
    hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false,
  });
}

/** Convert a BotStatus snapshot into a MetricsPoint with the given timestamp. */
function snapshotToPoint(snap: any, ts: number): MetricsPoint {
  const s = snap?.system;
  if (!s) {
    return {
      time: makeTime(ts), timestamp: ts,
      systemCpuPercent: null, cpuPercent: null,
      systemMemoryPercent: null, memoryPercent: null,
      systemMemoryUsedMb: null, memoryRssMb: null, memoryTotalMb: null,
    };
  }
  const total = s.memory_total_mb || 0;
  return {
    time: makeTime(ts),
    timestamp: ts,
    systemCpuPercent: s.system_cpu_percent ?? 0,
    cpuPercent: s.cpu_percent ?? null,
    systemMemoryPercent: total > 0 ? ((s.memory_used_mb ?? 0) / total) * 100 : 0,
    memoryPercent: total > 0 ? (s.memory_rss_mb / total) * 100 : 0,
    systemMemoryUsedMb: s.memory_used_mb ?? 0,
    memoryRssMb: s.memory_rss_mb ?? 0,
    memoryTotalMb: total,
  };
}

export function useMetricsHistory(maxPoints: number = MAX_POINTS) {
  const [history, setHistory] = useState<MetricsPoint[]>([]);
  const bufferRef = useRef<MetricsPoint[]>([]);

  useEffect(() => {
    let active = true;

    // ── Phase 1: back-fill from server-side ring buffer ─────────────────
    api.metricsHistory().then(({ snapshots }) => {
      if (!active) return;

      const histPoints = snapshots
        .map(s => snapshotToPoint(s, s._ts))
        .filter(p => p.cpuPercent !== null || p.memoryPercent !== null);

      // Merge with any live points that arrived during the fetch
      const all = [...histPoints, ...bufferRef.current]
        .sort((a, b) => a.timestamp - b.timestamp);

      // Deduplicate: when two points share a timestamp, keep the one with data
      const deduped: MetricsPoint[] = [];
      for (const p of all) {
        const prev = deduped[deduped.length - 1];
        if (prev && p.timestamp === prev.timestamp) {
          if (p.cpuPercent !== null && prev.cpuPercent === null) {
            deduped[deduped.length - 1] = p;
          }
        } else {
          deduped.push(p);
        }
      }

      bufferRef.current = deduped.slice(-maxPoints);
      setHistory(bufferRef.current);
    }).catch(() => {
      // Proceed with polling even if history fetch fails
    });

    // ── Phase 2: incremental 1-second polling ───────────────────────────
    const poll = async () => {
      try {
        const status = await api.status();
        if (!active || !status?.system) return;

        const now = Date.now();
        const point = snapshotToPoint(status, now);
        if (point.cpuPercent === null && point.memoryPercent === null) return;

        bufferRef.current = [...bufferRef.current, point].slice(-maxPoints);
        setHistory(bufferRef.current);
      } catch { /* transient */ }
    };

    // Start polling immediately — Phase 1 & 2 run concurrently.
    // Phase 2 points that arrive before the history fetch resolves are
    // merged into the result in Phase 1's .then() callback.
    const timer = setInterval(poll, POLL_MS);
    poll();

    return () => { active = false; clearInterval(timer); };
  }, [maxPoints]);

  return history;
}
