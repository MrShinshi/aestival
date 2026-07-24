/**
 * Registration page — create a new account with username + password.
 *
 * On success the server sets the session cookie so the user is
 * logged in immediately, then redirected to the dashboard.
 */

import { useState } from 'react';
import { useNavigate, Link } from 'react-router-dom';
import { useAuth } from '../lib/auth';
import { Navigate } from 'react-router-dom';

export default function RegisterPage() {
  const { isAuthenticated, isLoading, register } = useAuth();
  const navigate = useNavigate();

  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [confirmPassword, setConfirmPassword] = useState('');
  const [error, setError] = useState('');
  const [submitting, setSubmitting] = useState(false);

  if (!isLoading && isAuthenticated) {
    return <Navigate to="/" replace />;
  }

  if (isLoading) {
    return (
      <div className="flex items-center justify-center h-screen bg-[#0f0d1e]">
        <p className="text-gray-400">加载中…</p>
      </div>
    );
  }

  const handleRegister = async (e: React.FormEvent) => {
    e.preventDefault();
    setError('');

    // Client-side validation
    if (!username.trim()) {
      setError('用户名不能为空');
      return;
    }
    if (username.trim().length < 2 || username.trim().length > 32) {
      setError('用户名长度需在 2-32 个字符之间');
      return;
    }
    if (!/^[\w一-鿿-]{2,32}$/.test(username.trim())) {
      setError('用户名只能包含字母、数字、下划线、连字符和中文');
      return;
    }
    if (!password) {
      setError('密码不能为空');
      return;
    }
    if (password.length < 8) {
      setError('密码长度至少为 8 个字符');
      return;
    }
    if (password !== confirmPassword) {
      setError('两次输入的密码不一致');
      return;
    }

    setSubmitting(true);
    const result = await register(username.trim(), password);
    setSubmitting(false);

    if (result.success) {
      navigate('/', { replace: true });
    } else {
      setError(result.error || '注册失败，请稍后重试');
    }
  };

  return (
    <div className="flex items-center justify-center min-h-screen bg-[#0f0d1e]">
      <div className="w-full max-w-sm px-4">
        <div className="text-center mb-8">
          <h1 className="text-2xl font-bold text-indigo-400">创建账号</h1>
          <p className="text-sm text-gray-500 mt-1">aestival dashboard</p>
        </div>

        <div className="bg-gray-900 border border-gray-800 rounded-lg p-6">
          <form onSubmit={handleRegister} className="space-y-3">
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
              placeholder="密码（至少 8 个字符）"
              className="w-full px-3 py-2 bg-gray-800 border border-gray-700 rounded
                         text-sm text-gray-200 placeholder-gray-500
                         focus:outline-none focus:border-indigo-500 transition-colors"
              autoComplete="new-password"
              disabled={submitting}
            />
            <input
              type="password"
              value={confirmPassword}
              onChange={(e) => setConfirmPassword(e.target.value)}
              placeholder="确认密码"
              className="w-full px-3 py-2 bg-gray-800 border border-gray-700 rounded
                         text-sm text-gray-200 placeholder-gray-500
                         focus:outline-none focus:border-indigo-500 transition-colors"
              autoComplete="new-password"
              disabled={submitting}
            />
            <button
              type="submit"
              disabled={submitting || !username.trim() || !password || !confirmPassword}
              className="w-full px-4 py-2.5 bg-indigo-600 hover:bg-indigo-700
                         disabled:opacity-50 disabled:cursor-not-allowed
                         rounded text-sm font-medium transition-colors text-white"
            >
              {submitting ? '注册中…' : '注册'}
            </button>
          </form>

          <p className="text-center text-xs text-gray-500 mt-4">
            已有账号？
            <Link to="/login" className="text-indigo-400 hover:text-indigo-300 ml-1 transition-colors">
              返回登录
            </Link>
          </p>
        </div>

        {error && (
          <div className="mt-4 p-3 bg-red-900/30 border border-red-800/50 rounded text-sm text-red-400">
            {error}
          </div>
        )}
      </div>
    </div>
  );
}
