import { NavLink, useNavigate } from 'react-router-dom';
import { useAuth } from '../lib/auth';
import type { ReactNode } from 'react';

const navItems = [
  { to: '/', label: '仪表盘', icon: '◈' },
  { to: '/agents', label: 'Agent', icon: '◇' },
  { to: '/conversations', label: '对话', icon: '◉' },
  { to: '/logs', label: '日志', icon: '▤' },
  { to: '/settings', label: '设置', icon: '⚙' },
];

export default function Layout({ children }: { children: ReactNode }) {
  const { user, logout } = useAuth();
  const navigate = useNavigate();

  const handleLogout = async () => {
    await logout();
    navigate('/');
  };

  return (
    <div className="flex h-screen">
      {/* Sidebar */}
      <aside className="w-56 bg-gray-900 border-r border-gray-800 flex flex-col">
        <div className="p-4 border-b border-gray-800">
          <h1 className="text-lg font-bold text-indigo-400">绯英管理</h1>
          <p className="text-xs text-gray-500 mt-0.5">aestival dashboard</p>
        </div>

        <nav className="flex-1 p-2 space-y-0.5">
          {navItems.map(item => (
            <NavLink
              key={item.to}
              to={item.to}
              end={item.to === '/'}
              className={({ isActive }) =>
                `flex items-center gap-2 px-3 py-2 rounded text-sm transition-colors ${
                  isActive
                    ? 'bg-indigo-900/50 text-indigo-300'
                    : 'text-gray-400 hover:text-gray-200 hover:bg-gray-800'
                }`
              }
            >
              <span>{item.icon}</span>
              {item.label}
            </NavLink>
          ))}
        </nav>

        <div className="p-4 border-t border-gray-800">
          <div className="text-xs text-gray-500 mb-2">{user}</div>
          <button
            onClick={handleLogout}
            className="text-xs text-gray-400 hover:text-gray-200 transition-colors"
          >
            退出登录
          </button>
        </div>
      </aside>

      {/* Main content */}
      <main className="flex-1 overflow-auto p-6">
        {children}
      </main>
    </div>
  );
}
