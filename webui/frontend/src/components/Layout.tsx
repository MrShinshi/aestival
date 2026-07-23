import { NavLink } from 'react-router-dom';
import type { ReactNode } from 'react';
import { LayoutDashboard, Bot, MessageSquare, FileText, Settings } from 'lucide-react';
import { APP_VERSION } from '../lib/api';

const navItems = [
  { to: '/', label: '仪表盘', Icon: LayoutDashboard },
  { to: '/agents', label: 'Agent', Icon: Bot },
  { to: '/conversations', label: '对话', Icon: MessageSquare },
  { to: '/logs', label: '日志', Icon: FileText },
  { to: '/settings', label: '设置', Icon: Settings },
];

export default function Layout({ children }: { children: ReactNode }) {
  return (
    <div className="flex h-screen">
      <aside className="w-56 bg-gray-900 border-r border-gray-800 flex flex-col" aria-label="Main navigation">
        <div className="p-4 border-b border-gray-800">
          <h1 className="text-lg font-bold text-indigo-400">绯英管理</h1>
          <p className="text-xs text-gray-500 mt-0.5">aestival dashboard</p>
        </div>

        <nav className="flex-1 p-2 space-y-0.5">
          {navItems.map(({ to, label, Icon }) => (
            <NavLink
              key={to}
              to={to}
              end={to === '/'}
              className={({ isActive }) =>
                `flex items-center gap-2 px-3 py-2 rounded text-sm transition-colors ${
                  isActive
                    ? 'bg-indigo-900/50 text-indigo-300'
                    : 'text-gray-400 hover:text-gray-200 hover:bg-gray-800'
                }`
              }
            >
              <Icon size={16} aria-hidden="true" />
              {label}
            </NavLink>
          ))}
        </nav>

        <div className="p-4 border-t border-gray-800">
          <div className="text-xs text-gray-500">aestival v{APP_VERSION}</div>
        </div>
      </aside>

      <main className="flex-1 overflow-auto p-6">
        {children}
      </main>
    </div>
  );
}
