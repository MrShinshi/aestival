/**
 * Custom hook: loads cached 2-minute metrics history on mount, then polls
 * /api/ui/status every second for incremental updates.
 *
 * The backend keeps a warm ring buffer (1 s resolution, 2 min window) via a
 * background poller, so the chart is immediately populated on page load
 * instead of starting empty.
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
const WINDOW_MS = 120_000;        // 2 minutes
const MAX_POINTS = WINDOW_MS / POLL_MS; // 120

function makeTime(ts: number): string {
  const d = new Date(ts);
  return d.toLocaleTimeString('zh-CN', {
    hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false,
  });
}

function nullPoint(ts: number): MetricsPoint {
  return {
    time: makeTime(ts),
    timestamp: ts,
    systemCpuPercent: null,
    cpuPercent: null,
    systemMemoryPercent: null,
    memoryPercent: null,
    systemMemoryUsedMb: null,
    memoryRssMb: null,
    memoryTotalMb: null,
  };
}

/** Build a full-width seeded buffer with null values — X axis shows 2 min span. */
function seedBuffer(): MetricsPoint[] {
  const now = Date.now();
  const pts: MetricsPoint[] = [];
  for (let i = 0; i < MAX_POINTS; i++) {
    const t = now - (MAX_POINTS - 1 - i) * POLL_MS;
    pts.push(nullPoint(t));
  }
  return pts;
}

/** Convert a BotStatus snapshot into a MetricsPoint. */
function snapshotToPoint(snap: any, ts: number): MetricsPoint {
  const s = snap.system;
  const total = s?.memory_total_mb || 0;
  return {
    time: makeTime(ts),
    timestamp: ts,
    systemCpuPercent: s?.system_cpu_percent ?? 0,
    cpuPercent: s?.cpu_percent ?? null,
    systemMemoryPercent: total > 0 ? ((s?.memory_used_mb ?? 0) / total) * 100 : 0,
    memoryPercent: total > 0 ? (s?.memory_rss_mb / total) * 100 : 0,
    systemMemoryUsedMb: s?.memory_used_mb ?? 0,
    memoryRssMb: s?.memory_rss_mb ?? 0,
    memoryTotalMb: total,
  };
}

/**
 * Merge historical snapshots into the seeded buffer.  Each snapshot is placed
 * in the slot whose timestamp is closest.  Later polled data will overwrite
 * on a real-time rolling basis.
 */
function mergeHistory(buffer: MetricsPoint[], snapshots: Array<{ _ts: number } & Record<string, any>>): MetricsPoint[] {
  if (!snapshots.length) return buffer;

  const cloned = [...buffer];
  for (const snap of snapshots) {
    const ts = snap._ts;
    // Find the closest slot
    let bestIdx = 0;
    let bestDist = Infinity;
    for (let i = 0; i < cloned.length; i++) {
      const dist = Math.abs(cloned[i].timestamp - ts);
      if (dist < bestDist) {
        bestDist = dist;
        bestIdx = i;
      }
    }
    // Only overwrite if within one poll interval (otherwise data is stale)
    if (bestDist <= POLL_MS * 1.5) {
      cloned[bestIdx] = snapshotToPoint(snap, cloned[bestIdx].timestamp);
    }
  }
  return cloned;
}

export function useMetricsHistory(maxPoints: number = MAX_POINTS) {
  const [history, setHistory] = useState<MetricsPoint[]>(() => seedBuffer());
  const bufferRef = useRef<MetricsPoint[]>(history);
  const historyLoaded = useRef(false);

  useEffect(() => {
    let active = true;

    // ── Phase 1: back-fill from server-side cache ──────────────────────
    api.metricsHistory().then(({ snapshots }) => {
      if (!active) return;
      bufferRef.current = mergeHistory(bufferRef.current, snapshots);
      setHistory(bufferRef.current);
      historyLoaded.current = true;
    }).catch(() => {
      // Proceed with polling even if history fetch fails
      historyLoaded.current = true;
    });

    // ── Phase 2: incremental 1-second polling ─────────────────────────
    const poll = async () => {
      try {
        const status = await api.status();
        if (!active || !status?.system) return;

        const now = Date.now();
        const point = snapshotToPoint(status, now);

        bufferRef.current = [...bufferRef.current, point].slice(-maxPoints);
        setHistory(bufferRef.current);
      } catch { /* transient */ }
    };

    // Start polling immediately — Phase 1 & 2 run concurrently.
    // Phase 2 points fill gaps the history didn't cover.
    const timer = setInterval(poll, POLL_MS);
    poll();

    return () => { active = false; clearInterval(timer); };
  }, [maxPoints]);

  return history;
}
