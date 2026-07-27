interface CpuGaugeProps {
  percent: number;
  size?: number;
}

export default function CpuGauge({ percent, size = 120 }: CpuGaugeProps) {
  const clamped = Math.max(0, Math.min(100, percent));
  const strokeWidth = 10;
  const radius = (size - strokeWidth) / 2;
  const center = size / 2;

  // Arc path for a semi-circle (from -π to 0, top half)
  const startAngle = -Math.PI;
  const endAngle = 0;
  const fillAngle = startAngle + (endAngle - startAngle) * (clamped / 100);

  const x1 = center + radius * Math.cos(startAngle);
  const y1 = center + radius * Math.sin(startAngle);
  const x2 = center + radius * Math.cos(fillAngle);
  const y2 = center + radius * Math.sin(fillAngle);
  const largeArc = fillAngle - startAngle > Math.PI ? 1 : 0;

  const bgColor = 'stroke-gray-700';
  const fillColor =
    clamped >= 80 ? 'stroke-red-400' :
    clamped >= 50 ? 'stroke-yellow-400' :
    'stroke-green-400';

  return (
    <svg width={size} height={size * 0.75} viewBox={`0 0 ${size} ${size}`}>
      {/* Background arc */}
      <path
        d={`M ${x1} ${y1} A ${radius} ${radius} 0 ${largeArc} 1 ${center + radius} ${center}`}
        fill="none"
        className={bgColor}
        strokeWidth={strokeWidth}
        strokeLinecap="round"
      />
      {/* Fill arc */}
      {clamped > 0 && (
        <path
          d={`M ${x1} ${y1} A ${radius} ${radius} 0 ${largeArc} 1 ${x2} ${y2}`}
          fill="none"
          className={fillColor}
          strokeWidth={strokeWidth}
          strokeLinecap="round"
          style={{ transition: 'd 0.5s ease' }}
        />
      )}
      {/* Percentage text */}
      <text
        x={center}
        y={center - 4}
        textAnchor="middle"
        className="fill-gray-100 text-lg font-bold"
        fontSize="18"
      >
        {clamped.toFixed(1)}%
      </text>
      <text
        x={center}
        y={center + 14}
        textAnchor="middle"
        className="fill-gray-500"
        fontSize="10"
      >
        CPU
      </text>
    </svg>
  );
}
