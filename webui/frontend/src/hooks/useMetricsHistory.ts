/**
 * Custom hook: loads cached 2-minute metrics history on mount, then polls
 * /api/ui/status every second for incremental updates.
 *
 * The backend keeps a warm ring buffer (1 s resolution, 3 min window) via a
 * background poller.  History snapshots carry server-side timestamps (_ts)
 * so they are inserted at their true position — no pre-allocated seed slots,
 * no clock-skew rejection.
 *
 * On first load the history fetch and initial poll run concurrently; results
 * are merged, deduped, and rendered in one shot so the chart shows a full
 * 2-minute range immediately.
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
const MAX_POINTS = 120; // 2 minutes display window at 1-second resolution

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

/**
 * Merge and deduplicate an array of points sorted by timestamp.
 * When two points share the exact same timestamp the one with non-null
 * CPU/memory values wins.
 */
function mergeAndDedup(points: MetricsPoint[]): MetricsPoint[] {
  const deduped: MetricsPoint[] = [];
  for (const p of points) {
    const prev = deduped[deduped.length - 1];
    if (prev && p.timestamp === prev.timestamp) {
      if (p.cpuPercent !== null && prev.cpuPercent === null) {
        deduped[deduped.length - 1] = p;
      }
    } else {
      deduped.push(p);
    }
  }
  return deduped;
}

export function useMetricsHistory(maxPoints: number = MAX_POINTS) {
  const [history, setHistory] = useState<MetricsPoint[]>([]);
  const bufferRef = useRef<MetricsPoint[]>([]);
  const historyLoadedRef = useRef(false);

  useEffect(() => {
    let active = true;
    // Reset for Strict Mode double-mount
    historyLoadedRef.current = false;
    bufferRef.current = [];

    // ── Phase 1: back-fill from server-side ring buffer ─────────────────
    // Runs concurrently with Phase 2; live points that arrive while the
    // history fetch is in-flight are stashed in bufferRef and merged in.
    const historyPromise = api.metricsHistory().then(({ snapshots }) => {
      if (!active) return;

      const histPoints = snapshots
        .map(s => snapshotToPoint(s, s._ts))
        .filter(p => p.cpuPercent !== null || p.memoryPercent !== null);

      // Merge history with any live points accumulated during the fetch
      const all = [...histPoints, ...bufferRef.current]
        .sort((a, b) => a.timestamp - b.timestamp);

      bufferRef.current = mergeAndDedup(all).slice(-maxPoints);
      historyLoadedRef.current = true;
      setHistory(bufferRef.current);
    }).catch(() => {
      // Proceed with polling even if history fetch fails
      historyLoadedRef.current = true;
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
        // Only push updates after the history back-fill has landed (or
        // failed), so the chart never briefly renders a single live point
        // and then jumps — the first render always has the full window.
        if (historyLoadedRef.current) {
          setHistory(bufferRef.current);
        }
      } catch { /* transient */ }
    };

    // Start polling immediately — live points accumulate in bufferRef.
    // Once historyPromise resolves they are merged in; after that every
    // poll pushes directly to state.
    const timer = setInterval(poll, POLL_MS);
    poll();

    // Safety: if history never resolves (network partition), force-unblock
    // the live feed after 3 seconds so the chart doesn't stay blank.
    const unblockTimer = setTimeout(() => {
      if (!historyLoadedRef.current) {
        historyLoadedRef.current = true;
        if (bufferRef.current.length > 0) {
          setHistory(bufferRef.current);
        }
      }
    }, 3000);

    return () => {
      active = false;
      clearInterval(timer);
      clearTimeout(unblockTimer);
    };
  }, [maxPoints]);

  return history;
}
