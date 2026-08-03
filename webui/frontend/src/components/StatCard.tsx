import type { LucideIcon } from 'lucide-react';

interface StatCardProps {
  label: string;
  value: string | number;
  color: 'indigo' | 'green' | 'red' | 'yellow' | 'blue' | 'gray';
  icon?: LucideIcon;
  subtitle?: string;
}

const borderMap: Record<string, string> = {
  indigo: 'border-indigo-800 bg-indigo-950/30',
  green: 'border-green-800 bg-green-950/30',
  red: 'border-red-800 bg-red-950/30',
  yellow: 'border-yellow-800 bg-yellow-950/30',
  blue: 'border-blue-800 bg-blue-950/30',
  gray: 'border-gray-800 bg-gray-900',
};

const valueColorMap: Record<string, string> = {
  indigo: 'text-indigo-400',
  green: 'text-green-400',
  red: 'text-red-400',
  yellow: 'text-yellow-400',
  blue: 'text-blue-400',
  gray: 'text-gray-400',
};

export default function StatCard({ label, value, color, icon: Icon, subtitle }: StatCardProps) {
  return (
    <div className={`rounded-lg border p-4 ${borderMap[color]}`}>
      <div className="text-xs text-gray-500 mb-1 flex items-center gap-1.5">
        {Icon && <Icon size={12} />}
        {label}
      </div>
      <div className={`text-2xl font-bold ${valueColorMap[color]}`}>{value}</div>
      {subtitle && <div className="text-xs text-gray-500 mt-1">{subtitle}</div>}
    </div>
  );
}
