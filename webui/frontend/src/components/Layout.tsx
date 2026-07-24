import { NavLink } from 'react-router-dom';
import type { ReactNode } from 'react';
import {
  LayoutDashboard,
  Bot,
  MessageSquare,
  FileText,
  Settings,
  LogOut,
  User,
  Github,
} from 'lucide-react';
import { APP_VERSION } from '../lib/api';
import { useAuth } from '../lib/auth';

const navItems = [
  { to: '/', label: '仪表盘', Icon: LayoutDashboard },
  { to: '/agents', label: 'Agent', Icon: Bot },
  { to: '/conversations', label: '对话', Icon: MessageSquare },
  { to: '/logs', label: '日志', Icon: FileText },
  { to: '/settings', label: '设置', Icon: Settings },
];

export default function Layout({ children }: { children: ReactNode }) {
  const { user, linkedAccounts, logout } = useAuth();

  return (
    <div className="flex h-screen">
      <aside
        className="w-56 bg-gray-900 border-r border-gray-800 flex flex-col"
        aria-label="Main navigation"
      >
        {/* Brand header */}
        <div className="p-4 border-b border-gray-800">
          <h1 className="text-lg font-bold text-indigo-400">绯英管理</h1>
          <p className="text-xs text-gray-500 mt-0.5">aestival dashboard</p>
        </div>

        {/* Navigation */}
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

        {/* User section */}
        <div className="p-3 border-t border-gray-800 space-y-2">
          {user && (
            <div className="flex items-center gap-2 px-1">
              {user.avatar_url ? (
                <img
                  src={user.avatar_url}
                  alt={user.username}
                  className="w-7 h-7 rounded-full"
                />
              ) : (
                <User size={16} className="text-gray-400" />
              )}
              <div className="flex-1 min-w-0">
                <p className="text-sm text-gray-300 truncate">
                  {user.username}
                </p>
                {/* Platform badges */}
                <div className="flex gap-1 mt-0.5">
                  {linkedAccounts.some((a) => a.provider === 'github') && (
                    <span className="text-xs text-gray-500" title="已绑定 GitHub">
                      <Github size={12} aria-hidden="true" />
                    </span>
                  )}
                  {linkedAccounts.some((a) => a.provider === 'qq') && (
                    <span className="text-xs text-gray-500" title="已绑定 QQ">
                      <svg width="12" height="12" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
                        <path d="M12.003 2c-2.265 0-6.29 1.364-6.29 7.325v1.195S3.55 14.96 3.55 17.474c0 .665.17 1.025.567 1.41.724.706 1.645.73 1.645.73h.083c.294 0 .56-.037.793-.09-.035.174-.055.352-.055.537 0 1.193.942 2.693 2.398 2.971.194.037.392.058.595.058.624 0 1.226-.174 1.666-.466.37.21.846.349 1.358.376h.002c.512-.027.988-.166 1.358-.376.44.292 1.042.466 1.666.466.203 0 .4-.02.595-.058 1.456-.278 2.398-1.778 2.398-2.97 0-.186-.02-.364-.055-.538.233.053.499.09.793.09h.083s.921-.024 1.645-.73c.397-.385.567-.745.567-1.41 0-2.514-2.163-6.954-2.163-6.954V9.325C18.293 3.364 14.268 2 12.003 2z" />
                      </svg>
                    </span>
                  )}
                </div>
              </div>
            </div>
          )}

          <button
            onClick={logout}
            className="flex items-center gap-2 w-full px-2 py-1.5 rounded text-xs
                       text-gray-500 hover:text-gray-300 hover:bg-gray-800
                       transition-colors"
            title="退出登录"
          >
            <LogOut size={14} aria-hidden="true" />
            退出登录
          </button>
        </div>

        {/* Version */}
        <div className="px-4 pb-3">
          <div className="text-xs text-gray-500">aestival v{APP_VERSION}</div>
        </div>
      </aside>

      <main className="flex-1 overflow-auto p-6">{children}</main>
    </div>
  );
}
