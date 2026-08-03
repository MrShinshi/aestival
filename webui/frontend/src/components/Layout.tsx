import { NavLink, useLocation } from 'react-router-dom';
import { useEffect, useState } from 'react';
import type { ReactNode } from 'react';
import {
  LayoutDashboard,
  Bot,
  MessageSquare,
  FileText,
  Settings,
  LogOut,
  User,
  Shield,
  Github,
  Puzzle,
  Menu,
  X,
} from 'lucide-react';
import { APP_VERSION } from '../lib/api';
import { useAuth } from '../lib/auth';

interface NavItem {
  to: string;
  label: string;
  Icon: React.ComponentType<{ size?: number; 'aria-hidden'?: boolean }>;
  adminOnly?: boolean;
}

const navItems: NavItem[] = [
  { to: '/', label: '仪表盘', Icon: LayoutDashboard },
  { to: '/agents', label: 'Agent', Icon: Bot, adminOnly: true },
  { to: '/conversations', label: '对话', Icon: MessageSquare, adminOnly: true },
  { to: '/logs', label: '日志', Icon: FileText, adminOnly: true },
  { to: '/plugins', label: '插件', Icon: Puzzle, adminOnly: true },
  { to: '/settings', label: '设置', Icon: Settings },
];

export default function Layout({ children }: { children: ReactNode }) {
  const { user, linkedAccounts, isAdmin, logout } = useAuth();
  const [sidebarOpen, setSidebarOpen] = useState(false);
  const location = useLocation();

  // Close the mobile drawer whenever the route changes
  useEffect(() => { setSidebarOpen(false); }, [location.pathname]);

  // Lock body scroll while the drawer is open
  useEffect(() => {
    document.body.style.overflow = sidebarOpen ? 'hidden' : '';
    return () => { document.body.style.overflow = ''; };
  }, [sidebarOpen]);

  // Close the drawer on Escape
  useEffect(() => {
    if (!sidebarOpen) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') setSidebarOpen(false);
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [sidebarOpen]);

  const visibleItems = navItems.filter(item => !item.adminOnly || isAdmin);

  return (
    <div className="h-dvh flex flex-col">
      {/* ── Mobile top bar ────────────────────────────────────────────── */}
      <header className="lg:hidden flex items-center gap-3 px-4 py-3 bg-gray-900 border-b border-gray-800 shrink-0">
        <button
          onClick={() => setSidebarOpen(true)}
          className="-ml-1.5 p-1.5 text-gray-300 hover:text-white rounded transition-colors"
          aria-label="打开菜单"
        >
          <Menu size={20} aria-hidden="true" />
        </button>
        <h1 className="text-base font-bold text-indigo-400">绯英管理</h1>
      </header>

      {/* ── Overlay backdrop (mobile only) ────────────────────────────── */}
      {sidebarOpen && (
        <div
          className="fixed inset-0 z-40 bg-black/60 lg:hidden"
          onClick={() => setSidebarOpen(false)}
          aria-hidden="true"
        />
      )}

      <div className="flex flex-1 overflow-hidden">
        {/* ── Sidebar (desktop) / Drawer (mobile) ──────────────────── */}
        <aside
          className={`fixed inset-y-0 left-0 z-50 flex w-64 flex-col bg-gray-900 border-r border-gray-800
                      transition-transform duration-200 ease-in-out
                      ${sidebarOpen ? 'translate-x-0' : '-translate-x-full'}
                      lg:static lg:z-auto lg:translate-x-0 lg:w-56 lg:transition-none`}
          aria-label="Main navigation"
        >
          {/* Brand header */}
          <div className="p-4 border-b border-gray-800 flex items-center justify-between">
            <div>
              <h1 className="text-lg font-bold text-indigo-400">绯英管理</h1>
              <p className="text-xs text-gray-500 mt-0.5">aestival dashboard</p>
            </div>
            <button
              onClick={() => setSidebarOpen(false)}
              className="lg:hidden p-1 text-gray-400 hover:text-gray-200 rounded transition-colors"
              aria-label="关闭菜单"
            >
              <X size={20} aria-hidden="true" />
            </button>
          </div>

          {/* Navigation */}
          <nav className="flex-1 p-2 space-y-0.5 overflow-y-auto">
            {visibleItems.map(({ to, label, Icon }) => (
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
                <Icon size={16} aria-hidden={true} />
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
                  <div className="flex items-center gap-1">
                    <p className="text-sm text-gray-300 truncate">
                      {user.username}
                    </p>
                    {isAdmin && (
                      <span title="管理员" className="text-indigo-400">
                        <Shield size={12} aria-hidden={true} />
                      </span>
                    )}
                  </div>
                  {/* Platform badges */}
                  <div className="flex gap-1 mt-0.5">
                    {linkedAccounts.some((a) => a.provider === 'github') && (
                      <span className="text-xs text-gray-500" title="已绑定 GitHub">
                        <Github size={12} aria-hidden="true" />
                      </span>
                    )}
                    {linkedAccounts.some((a) => a.provider === 'qq') && (
                      <span className="text-xs text-gray-500" title="已绑定 QQ">
                        <svg width="12" height="12" viewBox="0 0 24 24" fill="currentColor" aria-hidden={true}>
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

        <main className="flex-1 overflow-auto p-4 sm:p-6">{children}</main>
      </div>
    </div>
  );
}
