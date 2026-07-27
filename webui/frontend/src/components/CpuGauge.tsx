/**
 * Semi-circle gauge — shows CPU percentage or memory usage in the same visual style.
 */
interface CpuGaugeProps {
  label: string;              // "CPU" or "内存"
  percent: number;            // 0–100 fill amount
  displayValue: string;       // main number shown, e.g. "23.5%", "512 MB"
  subValue?: string;          // optional smaller text below, e.g. "/ 4096 MB"
  color?: string;             // override the auto color
  size?: number;
}

const colorFor = (pct: number, override?: string) => {
  if (override) return override;
  if (pct >= 80) return 'stroke-red-400';
  if (pct >= 50) return 'stroke-yellow-400';
  return 'stroke-green-400';
};

export default function CpuGauge({
  label, percent, displayValue, subValue, color, size = 120,
}: CpuGaugeProps) {
  const clamped = Math.max(0, Math.min(100, percent));
  const strokeWidth = 10;
  const radius = (size - strokeWidth) / 2;
  const center = size / 2;

  // Arc path for a semi-circle (top half: from -π to 0)
  const startAngle = -Math.PI;
  const endAngle = 0;
  const fillAngle = startAngle + (endAngle - startAngle) * (clamped / 100);

  const x1 = center + radius * Math.cos(startAngle);
  const y1 = center + radius * Math.sin(startAngle);
  const x2 = center + radius * Math.cos(fillAngle);
  const y2 = center + radius * Math.sin(fillAngle);
  const largeArc = 0;

  const fillStroke = colorFor(clamped, color);

  return (
    <svg width={size} height={size * 0.75} viewBox={`0 0 ${size} ${size}`}>
      {/* Background arc */}
      <path
        d={`M ${x1} ${y1} A ${radius} ${radius} 0 ${largeArc} 1 ${center + radius} ${center}`}
        fill="none"
        className="stroke-gray-700"
        strokeWidth={strokeWidth}
        strokeLinecap="round"
      />
      {/* Fill arc */}
      {clamped > 0 && (
        <path
          d={`M ${x1} ${y1} A ${radius} ${radius} 0 ${largeArc} 1 ${x2} ${y2}`}
          fill="none"
          className={fillStroke}
          strokeWidth={strokeWidth}
          strokeLinecap="round"
          style={{ transition: 'd 0.5s ease' }}
        />
      )}
      {/* Main value */}
      <text
        x={center}
        y={center - 6}
        textAnchor="middle"
        className="fill-gray-100 text-lg font-bold"
        fontSize="17"
      >
        {displayValue}
      </text>
      {/* Secondary line */}
      {subValue ? (
        <text
          x={center}
          y={center + 10}
          textAnchor="middle"
          className="fill-gray-500"
          fontSize="10"
        >
          {subValue}
        </text>
      ) : (
        <text
          x={center}
          y={center + 12}
          textAnchor="middle"
          className="fill-gray-500"
          fontSize="10"
        >
          {label}
        </text>
      )}
    </svg>
  );
}
