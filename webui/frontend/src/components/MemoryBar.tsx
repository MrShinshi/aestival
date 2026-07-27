interface MemoryBarProps {
  rssMb: number;
  virtualMb?: number;
  maxMb?: number;
}

function fmtMB(mb: number): string {
  if (mb >= 1024) return `${(mb / 1024).toFixed(1)} GB`;
  return `${mb.toFixed(0)} MB`;
}

export default function MemoryBar({ rssMb, virtualMb, maxMb }: MemoryBarProps) {
  const displayMax = maxMb || Math.max(rssMb * 1.5, virtualMb || 0, 512);
  const rssPct = Math.min(100, (rssMb / displayMax) * 100);
  const virtPct = virtualMb ? Math.min(100, (virtualMb / displayMax) * 100) : 0;

  return (
    <div className="w-full">
      <div className="flex justify-between text-xs text-gray-500 mb-1">
        <span>RSS: {fmtMB(rssMb)}</span>
        {virtualMb !== undefined && <span>Virtual: {fmtMB(virtualMb)}</span>}
      </div>
      <div className="relative h-4 bg-gray-800 rounded-full overflow-hidden">
        {/* Virtual memory (lighter, wider bar behind) */}
        {virtPct > 0 && (
          <div
            className="absolute inset-y-0 left-0 bg-indigo-900/50 rounded-full"
            style={{ width: `${virtPct}%`, transition: 'width 0.5s ease' }}
          />
        )}
        {/* RSS (solid bar on top) */}
        <div
          className="absolute inset-y-0 left-0 bg-indigo-500 rounded-full"
          style={{ width: `${rssPct}%`, transition: 'width 0.5s ease' }}
        />
      </div>
    </div>
  );
}
