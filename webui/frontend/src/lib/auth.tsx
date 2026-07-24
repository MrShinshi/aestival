/**
 * Authentication context for the frontend.
 *
 * On mount, calls /api/ui/auth/me to determine the current session state.
 * Provides login/logout helpers and exposes the authenticated user + linked
 * OAuth accounts to the rest of the app.
 */

import React, { createContext, useContext, useState, useEffect, useCallback } from 'react';
import type { ReactNode } from 'react';

// ── Types ──────────────────────────────────────────────────────────────────

export interface AuthUser {
  id: string;
  username: string;
  avatar_url: string;
}

export interface LinkedAccount {
  id: string;
  provider: 'github' | 'qq';
  provider_username: string;
  created_at: string;
}

interface AuthState {
  user: AuthUser | null;
  linkedAccounts: LinkedAccount[];
  hasPassword: boolean;
  isLoading: boolean;
  isAuthenticated: boolean;
  login: (provider: 'github' | 'qq') => void;
  logout: () => void;
  refresh: () => Promise<void>;
  loginWithCredentials: (username: string, password: string) => Promise<{ success: boolean; error?: string }>;
  register: (username: string, password: string) => Promise<{ success: boolean; error?: string }>;
}

// ── Context ────────────────────────────────────────────────────────────────

const AuthContext = createContext<AuthState>({
  user: null,
  linkedAccounts: [],
  hasPassword: false,
  isLoading: true,
  isAuthenticated: false,
  login: () => {},
  logout: () => {},
  refresh: async () => {},
  loginWithCredentials: async () => ({ success: false }),
  register: async () => ({ success: false }),
});

export function useAuth(): AuthState {
  return useContext(AuthContext);
}

// ── Provider ───────────────────────────────────────────────────────────────

export function AuthProvider({ children }: { children: ReactNode }) {
  const [user, setUser] = useState<AuthUser | null>(null);
  const [linkedAccounts, setLinkedAccounts] = useState<LinkedAccount[]>([]);
  const [hasPassword, setHasPassword] = useState(false);
  const [isLoading, setIsLoading] = useState(true);

  const refresh = useCallback(async () => {
    try {
      const resp = await fetch('/api/ui/auth/me', {
        credentials: 'include',
      });
      if (resp.ok) {
        const data = await resp.json();
        if (data.authenticated) {
          setUser(data.user);
          setLinkedAccounts(data.linked_accounts || []);
          setHasPassword(data.has_password || false);
        } else {
          setUser(null);
          setLinkedAccounts([]);
          setHasPassword(false);
        }
      }
    } catch {
      // Network error — don't clear auth state
    } finally {
      setIsLoading(false);
    }
  }, []);

  useEffect(() => {
    refresh();
  }, [refresh]);

  const login = (provider: 'github' | 'qq') => {
    // Simple redirect — the backend handles the OAuth flow
    window.location.href = `/api/ui/auth/${provider}`;
  };

  const logout = () => {
    window.location.href = '/api/ui/auth/logout';
  };

  const loginWithCredentials = useCallback(async (username: string, password: string) => {
    try {
      const resp = await fetch('/api/ui/auth/login', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        credentials: 'include',
        body: JSON.stringify({ username, password }),
      });
      if (resp.ok) {
        await refresh();
        return { success: true as const };
      }
      const data = await resp.json().catch(() => ({ error: '登录失败' }));
      return { success: false as const, error: data.error || '登录失败' };
    } catch {
      return { success: false as const, error: '网络错误，请重试' };
    }
  }, [refresh]);

  const register = useCallback(async (username: string, password: string) => {
    try {
      const resp = await fetch('/api/ui/auth/register', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        credentials: 'include',
        body: JSON.stringify({ username, password }),
      });
      if (resp.ok) {
        await refresh();
        return { success: true as const };
      }
      const data = await resp.json().catch(() => ({ error: '注册失败' }));
      return { success: false as const, error: data.error || '注册失败' };
    } catch {
      return { success: false as const, error: '网络错误，请重试' };
    }
  }, [refresh]);

  return (
    <AuthContext.Provider
      value={{
        user,
        linkedAccounts,
        hasPassword,
        isLoading,
        isAuthenticated: !!user,
        login,
        logout,
        refresh,
        loginWithCredentials,
        register,
      }}
    >
      {children}
    </AuthContext.Provider>
  );
}
