/**
 * Client-side uptime ticker + formatter.
 *
 * Captures the initial server uptime value once, then increments every
 * second purely on the client.  Never re-syncs — avoids the visual flicker
 * caused by periodic server polls overwriting the live counter.
 */
import { useState, useEffect, useRef } from 'react';

/** Format seconds as hh:mm:ss (or dd:hh:mm:ss when ≥ 1 day). */
export function fmtUptime(seconds: number): string {
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  const pad = (n: number) => String(n).padStart(2, '0');
  if (d > 0) return `${d}d ${pad(h)}:${pad(m)}:${pad(s)}`;
  return `${pad(h)}:${pad(m)}:${pad(s)}`;
}

export function useUptime(serverSeconds: number | undefined): number | undefined {
  const [live, setLive] = useState<number | undefined>(serverSeconds);
  const baseRef = useRef<{ seconds: number; at: number } | null>(null);
  const seeded = useRef(false);

  // Capture the initial server value ONCE.  Subsequent poll values are
  // ignored — the client-side interval keeps the counter alive.
  if (!seeded.current && serverSeconds !== undefined && serverSeconds !== null) {
    seeded.current = true;
    baseRef.current = { seconds: serverSeconds, at: Date.now() };
  }

  useEffect(() => {
    const timer = setInterval(() => {
      if (!baseRef.current) return;
      const elapsed = Math.floor((Date.now() - baseRef.current.at) / 1000);
      setLive(baseRef.current.seconds + elapsed);
    }, 1000);
    return () => clearInterval(timer);
  }, []);

  return live;
}
