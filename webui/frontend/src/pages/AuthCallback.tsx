/**
 * Post-OAuth redirect handler page.
 *
 * The Express backend redirects the browser here after a completed OAuth flow.
 * Query parameters:
 *   ?login=success&redirect=/settings   — auth succeeded, refresh session + redirect
 *   ?error=conflict&targetUserId=X&...  — account conflict detected, show merge UI
 *   ?error=invalid_state                — CSRF or expired state token
 */

import { useEffect, useState } from 'react';
import { useSearchParams, useNavigate } from 'react-router-dom';
import { useAuth } from '../lib/auth';

export default function AuthCallback() {
  const [searchParams] = useSearchParams();
  const navigate = useNavigate();
  const { refresh } = useAuth();
  const [merging, setMerging] = useState(false);
  const [mergeError, setMergeError] = useState('');

  const loginSuccess = searchParams.get('login') === 'success';
  const redirect = searchParams.get('redirect') || '/';
  const error = searchParams.get('error');
  const provider = searchParams.get('provider') || '';
  const targetUserId = searchParams.get('targetUserId');
  const targetUsername = searchParams.get('targetUsername');
  const sourceUserId = searchParams.get('sourceUserId');
  const sourceUsername = searchParams.get('sourceUsername');

  // Refresh the auth state from the new session cookie, then redirect
  useEffect(() => {
    if (!loginSuccess && !error) return;

    if (loginSuccess) {
      refresh().then(() => {
        navigate(redirect, { replace: true });
      });
    }
  }, [loginSuccess, error, redirect, refresh, navigate]);

  const handleMerge = async () => {
    if (!targetUserId || !sourceUserId) return;

    setMerging(true);
    setMergeError('');

    try {
      const resp = await fetch('/api/ui/auth/merge', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        credentials: 'include',
        body: JSON.stringify({ targetUserId, sourceUserId }),
      });

      if (resp.ok) {
        await refresh();
        navigate(redirect, { replace: true });
      } else {
        const data = await resp.json();
        setMergeError(data.error || '合并失败');
      }
    } catch {
      setMergeError('网络错误，请重试');
    } finally {
      setMerging(false);
    }
  };

  const handleCancel = () => {
    navigate('/settings', { replace: true });
  };

  // ── Conflict UI ──────────────────────────────────────────────────────
  if (error === 'conflict' && targetUserId && sourceUserId) {
    return (
      <div className="flex items-center justify-center min-h-screen bg-[#0f0d1e]">
        <div className="w-full max-w-md bg-gray-900 border border-gray-800 rounded-lg p-6">
          <h2 className="text-lg font-semibold text-gray-200 mb-4">
            账号冲突
          </h2>

          <p className="text-sm text-gray-400 mb-2">
            此{provider === 'github' ? 'GitHub' : 'QQ'}账号已绑定到另一个账户。
          </p>

          <div className="bg-gray-800 rounded p-4 mb-4 space-y-2">
            <div className="flex justify-between text-sm">
              <span className="text-gray-500">当前账户</span>
              <span className="text-gray-200 font-medium">{targetUsername}</span>
            </div>
            <div className="flex justify-between text-sm">
              <span className="text-gray-500">冲突账户</span>
              <span className="text-gray-200 font-medium">{sourceUsername}</span>
            </div>
          </div>

          <p className="text-sm text-gray-400 mb-6">
            是否将冲突账户的所有绑定迁移到当前账户？冲突账户将被删除。
          </p>

          {mergeError && (
            <div className="mb-4 p-3 bg-red-900/30 border border-red-800/50 rounded text-sm text-red-400">
              {mergeError}
            </div>
          )}

          <div className="flex gap-3">
            <button
              onClick={handleMerge}
              disabled={merging}
              className="flex-1 px-4 py-2 bg-indigo-600 hover:bg-indigo-700
                         disabled:opacity-50 disabled:cursor-not-allowed
                         rounded text-sm font-medium transition-colors"
            >
              {merging ? '合并中…' : '合并'}
            </button>
            <button
              onClick={handleCancel}
              disabled={merging}
              className="flex-1 px-4 py-2 bg-gray-700 hover:bg-gray-600
                         rounded text-sm font-medium transition-colors"
            >
              取消
            </button>
          </div>
        </div>
      </div>
    );
  }

  // ── Error UI ──────────────────────────────────────────────────────────
  if (error && error !== 'conflict') {
    const messages: Record<string, string> = {
      invalid_state: '登录请求已过期，请重新登录。',
      token_exchange: '授权验证失败，请重试。',
      user_fetch: '获取用户信息失败，请稍后重试。',
      server_error: '服务器错误，请稍后重试。',
      no_code: '授权被取消。',
    };

    return (
      <div className="flex items-center justify-center min-h-screen bg-[#0f0d1e]">
        <div className="w-full max-w-sm bg-gray-900 border border-gray-800 rounded-lg p-6 text-center">
          <h2 className="text-lg font-semibold text-gray-200 mb-2">登录失败</h2>
          <p className="text-sm text-gray-400 mb-6">
            {messages[error] || `未知错误：${error}`}
          </p>
          <a
            href="/login"
            className="inline-block px-4 py-2 bg-indigo-600 hover:bg-indigo-700
                       rounded text-sm font-medium transition-colors no-underline"
          >
            返回登录
          </a>
        </div>
      </div>
    );
  }

  // ── Loading state (waiting for refresh) ───────────────────────────────
  return (
    <div className="flex items-center justify-center h-screen bg-[#0f0d1e]">
      <div className="text-center">
        <div className="animate-spin h-8 w-8 border-2 border-indigo-500 border-t-transparent rounded-full mx-auto mb-3" />
        <p className="text-gray-400 text-sm">
          {loginSuccess ? '登录成功，正在跳转…' : '处理中…'}
        </p>
      </div>
    </div>
  );
}
