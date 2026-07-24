/**
 * Login page with credential form and GitHub/QQ OAuth buttons.
 *
 * All OAuth flows are server-side.  The browser never makes direct API
 * calls to GitHub — our server handles token exchange + user profile
 * fetch, avoiding GFW issues.
 */

import { useState } from 'react';
import { useSearchParams, Link } from 'react-router-dom';
import { useAuth } from '../lib/auth';
import { Navigate } from 'react-router-dom';
import { Github } from 'lucide-react';

// ── Simple QQ icon ──────────────────────────────────────────────────────────

function QQIcon({ size = 20 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
      <path d="M12.003 2c-2.265 0-6.29 1.364-6.29 7.325v1.195S3.55 14.96 3.55 17.474c0 .665.17 1.025.567 1.41.724.706 1.645.73 1.645.73h.083c.294 0 .56-.037.793-.09-.035.174-.055.352-.055.537 0 1.193.942 2.693 2.398 2.971.194.037.392.058.595.058.624 0 1.226-.174 1.666-.466.37.21.846.349 1.358.376h.002c.512-.027.988-.166 1.358-.376.44.292 1.042.466 1.666.466.203 0 .4-.02.595-.058 1.456-.278 2.398-1.778 2.398-2.97 0-.186-.02-.364-.055-.538.233.053.499.09.793.09h.083s.921-.024 1.645-.73c.397-.385.567-.745.567-1.41 0-2.514-2.163-6.954-2.163-6.954V9.325C18.293 3.364 14.268 2 12.003 2z" />
    </svg>
  );
}

// ── Login page ──────────────────────────────────────────────────────────────

export default function LoginPage() {
  const { isAuthenticated, isLoading, loginWithCredentials } = useAuth();
  const [searchParams] = useSearchParams();

  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [formError, setFormError] = useState('');
  const [submitting, setSubmitting] = useState(false);

  if (!isLoading && isAuthenticated) {
    return <Navigate to="/" replace />;
  }

  const oauthError = searchParams.get('error');
  const displayError = formError || (oauthError ? '登录失败，请重试。' : '');

  if (isLoading) {
    return (
      <div className="flex items-center justify-center h-screen bg-[#0f0d1e]">
        <p className="text-gray-400">加载中…</p>
      </div>
    );
  }

  const handleCredentialLogin = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!username.trim() || !password) return;
    setFormError('');
    setSubmitting(true);
    const result = await loginWithCredentials(username.trim(), password);
    setSubmitting(false);
    if (!result.success) {
      setFormError(result.error || '登录失败');
    }
    // On success, isAuthenticated becomes true → Navigate redirects to /
  };

  return (
    <div className="flex items-center justify-center min-h-screen bg-[#0f0d1e]">
      <div className="w-full max-w-sm px-4">
        <div className="text-center mb-8">
          <h1 className="text-2xl font-bold text-indigo-400">绯英管理</h1>
          <p className="text-sm text-gray-500 mt-1">aestival dashboard</p>
        </div>

        <div className="bg-gray-900 border border-gray-800 rounded-lg p-6 space-y-4">
          {/* ── Credential login form ─────────────────────────────── */}
          <form onSubmit={handleCredentialLogin} className="space-y-3">
            <input
              type="text"
              value={username}
              onChange={(e) => setUsername(e.target.value)}
              placeholder="用户名"
              className="w-full px-3 py-2 bg-gray-800 border border-gray-700 rounded
                         text-sm text-gray-200 placeholder-gray-500
                         focus:outline-none focus:border-indigo-500 transition-colors"
              autoComplete="username"
              disabled={submitting}
            />
            <input
              type="password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              placeholder="密码"
              className="w-full px-3 py-2 bg-gray-800 border border-gray-700 rounded
                         text-sm text-gray-200 placeholder-gray-500
                         focus:outline-none focus:border-indigo-500 transition-colors"
              autoComplete="current-password"
              disabled={submitting}
            />
            <button
              type="submit"
              disabled={submitting || !username.trim() || !password}
              className="w-full px-4 py-2.5 bg-indigo-600 hover:bg-indigo-700
                         disabled:opacity-50 disabled:cursor-not-allowed
                         rounded text-sm font-medium transition-colors text-white"
            >
              {submitting ? '登录中…' : '登录'}
            </button>
          </form>

          {/* ── Register link ─────────────────────────────────────── */}
          <p className="text-center text-xs text-gray-500">
            还没有账号？
            <Link to="/register" className="text-indigo-400 hover:text-indigo-300 ml-1 transition-colors">
              立即注册
            </Link>
          </p>

          {/* ── Divider ───────────────────────────────────────────── */}
          <div className="flex items-center gap-3">
            <div className="flex-1 h-px bg-gray-800" />
            <span className="text-xs text-gray-600">或使用第三方登录</span>
            <div className="flex-1 h-px bg-gray-800" />
          </div>

          {/* ── OAuth buttons ─────────────────────────────────────── */}
          <a
            href="/api/ui/auth/github"
            className="flex items-center justify-center gap-3 w-full px-4 py-2.5
                       bg-gray-800 hover:bg-gray-700 border border-gray-700
                       rounded text-gray-200 transition-colors text-sm font-medium
                       no-underline"
          >
            <Github size={20} aria-hidden="true" />
            使用 GitHub 登录
          </a>

          <a
            href="/api/ui/auth/qq"
            className="flex items-center justify-center gap-3 w-full px-4 py-2.5
                       bg-blue-900/30 hover:bg-blue-900/50 border border-blue-800/50
                       rounded text-blue-300 transition-colors text-sm font-medium
                       no-underline"
          >
            <QQIcon size={20} />
            使用 QQ 登录
          </a>

          <p className="text-xs text-gray-600 text-center pt-1">
            登录后可关联多个平台账号
          </p>
        </div>

        {displayError && (
          <div className="mt-4 p-3 bg-red-900/30 border border-red-800/50 rounded text-sm text-red-400">
            {displayError}
          </div>
        )}
      </div>
    </div>
  );
}
