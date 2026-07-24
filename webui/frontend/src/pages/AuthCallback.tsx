/**
 * Post-OAuth redirect handler.
 *
 * The server handles ALL GitHub API calls (token exchange + user profile).
 * The browser only talks to our own server — no direct GitHub API access
 * needed, which avoids GFW issues.
 *
 * Flow:
 *   1. Read code + state from URL params
 *   2. Read code_verifier from sessionStorage
 *   3. POST {code, state, code_verifier} to /api/ui/auth/github/token
 *   4. Handle success / conflict
 *
 * QQ:
 *   The backend handles the entire server-side OAuth flow.
 */

import { useEffect, useState, useRef } from 'react';
import { useSearchParams, useNavigate } from 'react-router-dom';
import { useAuth } from '../lib/auth';

export default function AuthCallback() {
  const [searchParams] = useSearchParams();
  const navigate = useNavigate();
  const { refresh } = useAuth();
  const [conflict, setConflict] = useState<{
    targetUserId: string;
    targetUsername: string;
    sourceUserId: string;
    sourceUsername: string;
  } | null>(null);
  const [merging, setMerging] = useState(false);
  const [mergeError, setMergeError] = useState('');
  const didRun = useRef(false);

  const error = searchParams.get('error');
  const loginSuccess = searchParams.get('login') === 'success';
  const redirect = searchParams.get('redirect') || '/';

  // ── GitHub exchange (runs once on mount) ────────────────────────────────
  useEffect(() => {
    if (didRun.current) return;
    if (error || loginSuccess || !searchParams.get('code')) return;
    didRun.current = true;

    const code = searchParams.get('code')!;
    const state = searchParams.get('state')!;

    // Verify state matches what we sent
    const storedState = sessionStorage.getItem('github_state');
    if (!storedState || storedState !== state) {
      navigate('/auth/callback?error=invalid_state&provider=github', { replace: true });
      return;
    }

    const codeVerifier = sessionStorage.getItem('github_code_verifier');
    sessionStorage.removeItem('github_code_verifier');
    sessionStorage.removeItem('github_state');
    sessionStorage.removeItem('github_client_id');
    sessionStorage.removeItem('github_redirect_uri');

    async function doExchange() {
      try {
        // Server handles ALL GitHub API calls.
        // Browser only talks to our server → no GFW issues.
        const body: Record<string, string> = { code, state };
        if (codeVerifier) body.code_verifier = codeVerifier;

        const resp = await fetch('/api/ui/auth/github/token', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          credentials: 'include',
          body: JSON.stringify(body),
        });

        if (!resp.ok) {
          const data = await resp.json().catch(() => ({}));
          const errMsg = (data.error || '').toLowerCase();
          if (errMsg.includes('state')) {
            navigate('/auth/callback?error=invalid_state&provider=github', { replace: true });
          } else {
            navigate('/auth/callback?error=server_error&provider=github', { replace: true });
          }
          return;
        }

        const data = await resp.json();

        // Conflict — show merge UI
        if (data.conflict) {
          setConflict({
            targetUserId: data.targetUserId,
            targetUsername: data.targetUsername,
            sourceUserId: data.sourceUserId,
            sourceUsername: data.sourceUsername,
          });
          return;
        }

        // Success
        await refresh();
        navigate(data.redirect || '/', { replace: true });
      } catch {
        navigate('/auth/callback?error=server_error&provider=github', { replace: true });
      }
    }

    doExchange();
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  // ── Server-side OAuth success (QQ) ────────────────────────────────────
  useEffect(() => {
    if (!loginSuccess) return;
    refresh().then(() => {
      navigate(redirect, { replace: true });
    });
  }, [loginSuccess, redirect, refresh, navigate]);

  // ── Merge handler ─────────────────────────────────────────────────────
  const handleMerge = async () => {
    if (!conflict || merging) return;
    setMerging(true);
    setMergeError('');

    try {
      const resp = await fetch('/api/ui/auth/merge', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        credentials: 'include',
        body: JSON.stringify({
          targetUserId: conflict.targetUserId,
          sourceUserId: conflict.sourceUserId,
        }),
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

  // ── Conflict UI ───────────────────────────────────────────────────────
  if (conflict) {
    return (
      <div className="flex items-center justify-center min-h-screen bg-[#0f0d1e]">
        <div className="w-full max-w-md bg-gray-900 border border-gray-800 rounded-lg p-6">
          <h2 className="text-lg font-semibold text-gray-200 mb-4">账号冲突</h2>
          <p className="text-sm text-gray-400 mb-2">
            此 GitHub 账号已绑定到另一个账户。
          </p>
          <div className="bg-gray-800 rounded p-4 mb-4 space-y-2">
            <div className="flex justify-between text-sm">
              <span className="text-gray-500">当前账户</span>
              <span className="text-gray-200 font-medium">{conflict.targetUsername}</span>
            </div>
            <div className="flex justify-between text-sm">
              <span className="text-gray-500">冲突账户</span>
              <span className="text-gray-200 font-medium">{conflict.sourceUsername}</span>
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
              onClick={() => navigate('/settings', { replace: true })}
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
  if (error) {
    const messages: Record<string, string> = {
      invalid_state: '登录请求已过期，请重新登录。',
      token_exchange: '授权验证失败，请重试。',
      user_fetch: '获取用户信息失败，请稍后重试。',
      server_error: '服务器错误，请稍后重试。',
      no_code: '授权被取消。',
      no_verifier: '登录信息丢失，请重新登录。',
      insecure_context: '当前页面未使用 HTTPS，无法完成安全登录。请使用 HTTPS 访问。',
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

  // ── Loading ───────────────────────────────────────────────────────────
  return (
    <div className="flex items-center justify-center h-screen bg-[#0f0d1e]">
      <div className="text-center">
        <div className="animate-spin h-8 w-8 border-2 border-indigo-500 border-t-transparent rounded-full mx-auto mb-3" />
        <p className="text-gray-400 text-sm">
          {searchParams.get('code')
            ? '正在登录…'
            : loginSuccess
              ? '登录成功，正在跳转…'
              : '处理中…'}
        </p>
      </div>
    </div>
  );
}
