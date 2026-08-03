/**
 * Authentication context for the frontend.
 *
 * On mount, calls /api/ui/auth/me to determine the current session state.
 * Provides login/logout helpers and exposes the authenticated user + linked
 * OAuth accounts to the rest of the app.
 */

import React, { createContext, useContext, useState, useEffect, useCallback } from 'react';
import type { ReactNode } from 'react';
import { auth as authApi } from './api';

// ── Types ──────────────────────────────────────────────────────────────────

export interface AuthUser {
  id: string;
  username: string;
  avatar_url: string;
  is_admin?: boolean;
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
  isAdmin: boolean;
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
  isAdmin: false,
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
  const [isAdmin, setIsAdmin] = useState(false);
  const [isLoading, setIsLoading] = useState(true);

  // Listen for auth:expired events from the API client (401 responses).
  // When triggered, clear user state; ProtectedRoute handles the /login redirect.
  useEffect(() => {
    const handler = () => {
      setUser(null);
      setLinkedAccounts([]);
      setHasPassword(false);
      setIsAdmin(false);
    };
    window.addEventListener('auth:expired', handler);
    return () => window.removeEventListener('auth:expired', handler);
  }, []);

  const refresh = useCallback(async () => {
    let retries = 0;
    const maxRetries = 2;
    while (retries <= maxRetries) {
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
            setIsAdmin(data.user?.is_admin || false);
          } else {
            setUser(null);
            setLinkedAccounts([]);
            setHasPassword(false);
            setIsAdmin(false);
          }
          break;
        } else if (resp.status === 401 || resp.status === 403) {
          // Server explicitly rejected — clear state.
          setUser(null);
          setLinkedAccounts([]);
          setHasPassword(false);
          setIsAdmin(false);
          break;
        }
        // 5xx — may be transient, retry after backoff.
        if (retries < maxRetries) {
          await new Promise(r => setTimeout(r, 1000 * (retries + 1)));
        }
        retries++;
      } catch {
        // Network error — retry with backoff, don't clear auth state.
        if (retries < maxRetries) {
          await new Promise(r => setTimeout(r, 1000 * (retries + 1)));
        }
        retries++;
      }
    }
    setIsLoading(false);
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
      await authApi.login(username, password);
      await refresh();
      return { success: true as const };
    } catch (err: any) {
      return { success: false as const, error: err.message || '登录失败' };
    }
  }, [refresh]);

  const register = useCallback(async (username: string, password: string) => {
    try {
      await authApi.register(username, password);
      await refresh();
      return { success: true as const };
    } catch (err: any) {
      return { success: false as const, error: err.message || '注册失败' };
    }
  }, [refresh]);

  return (
    <AuthContext.Provider
      value={{
        user,
        linkedAccounts,
        hasPassword,
        isAdmin,
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
