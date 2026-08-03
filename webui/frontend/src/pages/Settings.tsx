/**
 * Settings page — account management, linked platforms, and about info.
 */

import { useState } from 'react';
import { Github, Link2, Unlink } from 'lucide-react';
import { useAuth, LinkedAccount } from '../lib/auth';
import { APP_VERSION } from '../lib/api';

function QQBird({ size = 16 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
      <path d="M12.003 2c-2.265 0-6.29 1.364-6.29 7.325v1.195S3.55 14.96 3.55 17.474c0 .665.17 1.025.567 1.41.724.706 1.645.73 1.645.73h.083c.294 0 .56-.037.793-.09-.035.174-.055.352-.055.537 0 1.193.942 2.693 2.398 2.971.194.037.392.058.595.058.624 0 1.226-.174 1.666-.466.37.21.846.349 1.358.376h.002c.512-.027.988-.166 1.358-.376.44.292 1.042.466 1.666.466.203 0 .4-.02.595-.058 1.456-.278 2.398-1.778 2.398-2.97 0-.186-.02-.364-.055-.538.233.053.499.09.793.09h.083s.921-.024 1.645-.73c.397-.385.567-.745.567-1.41 0-2.514-2.163-6.954-2.163-6.954V9.325C18.293 3.364 14.268 2 12.003 2z" />
    </svg>
  );
}

interface ProviderConfig {
  key: 'github' | 'qq';
  label: string;
  Icon: React.ComponentType<{ size?: number }>;
  linkUrl: string;
  linkColor: string;
}

const providers: ProviderConfig[] = [
  {
    key: 'github',
    label: 'GitHub',
    Icon: Github,
    linkUrl: '/api/ui/auth/github?mode=link&redirect=/settings',
    linkColor: 'text-gray-300 bg-gray-700 hover:bg-gray-600',
  },
  {
    key: 'qq',
    label: 'QQ',
    Icon: QQBird,
    linkUrl: '/api/ui/auth/qq?mode=link&redirect=/settings',
    linkColor: 'text-blue-300 bg-blue-900/30 hover:bg-blue-900/50',
  },
];

export default function Settings() {
  const { user, linkedAccounts, refresh } = useAuth();
  const [unlinking, setUnlinking] = useState<string | null>(null);
  const [error, setError] = useState('');

  const handleUnlink = async (provider: string) => {
    if (unlinking) return;
    setUnlinking(provider);
    setError('');

    try {
      const resp = await fetch('/api/ui/auth/unlink', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        credentials: 'include',
        body: JSON.stringify({ provider }),
      });

      if (resp.ok) {
        await refresh();
      } else {
        const data = await resp.json();
        setError(data.error || '解绑失败');
      }
    } catch {
      setError('网络错误，请重试');
    } finally {
      setUnlinking(null);
    }
  };

  const linkedMap = new Map<string, LinkedAccount>();
  for (const a of linkedAccounts) {
    linkedMap.set(a.provider, a);
  }

  return (
    <div className="max-w-2xl space-y-8">
      {/* Account management */}
      <section>
        <h2 className="text-lg font-semibold text-gray-200 mb-4">账号管理</h2>
        <div className="bg-gray-900 border border-gray-800 rounded-lg p-4">
          {user ? (
            <div className="flex items-center gap-3">
              {user.avatar_url ? (
                <img
                  src={user.avatar_url}
                  alt={user.username}
                  className="w-10 h-10 rounded-full"
                />
              ) : (
                <div className="w-10 h-10 rounded-full bg-gray-700 flex items-center justify-center">
                  <span className="text-gray-400 text-sm font-medium">
                    {user.username.charAt(0).toUpperCase()}
                  </span>
                </div>
              )}
              <div>
                <p className="text-sm font-medium text-gray-200">
                  {user.username}
                </p>
                <p className="text-xs text-gray-500">ID: {user.id}</p>
              </div>
            </div>
          ) : (
            <p className="text-sm text-gray-400">加载中…</p>
          )}
        </div>
      </section>

      {/* Linked accounts */}
      <section>
        <h2 className="text-lg font-semibold text-gray-200 mb-4">绑定账号</h2>

        {error && (
          <div className="mb-3 p-3 bg-red-900/30 border border-red-800/50 rounded text-sm text-red-400">
            {error}
          </div>
        )}

        <div className="space-y-2">
          {providers.map(({ key, label, Icon, linkUrl, linkColor }) => {
            const linked = linkedMap.get(key);

            return (
              <div
                key={key}
                className="flex items-center justify-between bg-gray-900 border
                           border-gray-800 rounded-lg p-4"
              >
                <div className="flex items-center gap-3">
                  <Icon size={20} />
                  <div>
                    <p className="text-sm font-medium text-gray-200">{label}</p>
                    {linked ? (
                      <p className="text-xs text-gray-500">
                        已绑定：{linked.provider_username}
                      </p>
                    ) : (
                      <p className="text-xs text-gray-600">未绑定</p>
                    )}
                  </div>
                </div>

                {linked ? (
                  <button
                    onClick={() => handleUnlink(key)}
                    disabled={unlinking === key}
                    className="flex items-center gap-1.5 px-3 py-1.5 rounded text-xs
                               text-red-400 hover:text-red-300 hover:bg-red-900/20
                               disabled:opacity-50 disabled:cursor-not-allowed
                               transition-colors"
                  >
                    <Unlink size={14} aria-hidden="true" />
                    {unlinking === key ? '解绑中…' : '解绑'}
                  </button>
                ) : (
                  <a
                    href={linkUrl}
                    className={`flex items-center gap-1.5 px-3 py-1.5 rounded text-xs
                                border border-gray-700 transition-colors no-underline
                                ${linkColor}`}
                  >
                    <Link2 size={14} aria-hidden="true" />
                    绑定
                  </a>
                )}
              </div>
            );
          })}
        </div>

        <p className="text-xs text-gray-600 mt-3">
          登录后可关联多个平台账号。至少需要保留一个绑定方式。
        </p>
      </section>

      {/* About */}
      <section>
        <h2 className="text-lg font-semibold text-gray-200 mb-4">关于</h2>
        <div className="bg-gray-900 border border-gray-800 rounded-lg p-4 space-y-2">
          <div className="flex justify-between text-sm">
            <span className="text-gray-500">模式</span>
            <span className="text-gray-300">本地管理</span>
          </div>
          <div className="flex justify-between text-sm">
            <span className="text-gray-500">环境</span>
            <span className="text-gray-300">浏览器</span>
          </div>
          <div className="flex justify-between text-sm">
            <span className="text-gray-500">版本</span>
            <span className="text-gray-300">v{APP_VERSION}</span>
          </div>
        </div>
      </section>
    </div>
  );
}
