/**
 * Client-side uptime ticker + formatter.
 *
 * The server reports uptime_seconds at poll intervals (10–15s).
 * This hook advances the value every second so the displayed time
 * ticks up smoothly rather than jumping every poll cycle.
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

  useEffect(() => {
    if (serverSeconds === undefined || serverSeconds === null) {
      baseRef.current = null;
      setLive(undefined);
      return;
    }
    // Reset base whenever the server value changes (new poll).
    baseRef.current = { seconds: serverSeconds, at: Date.now() };
    setLive(serverSeconds);
  }, [serverSeconds]);

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
