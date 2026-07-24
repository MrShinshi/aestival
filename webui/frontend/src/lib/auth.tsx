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
  isLoading: boolean;
  isAuthenticated: boolean;
  login: (provider: 'github' | 'qq') => void;
  logout: () => void;
  refresh: () => Promise<void>;
}

// ── Context ────────────────────────────────────────────────────────────────

const AuthContext = createContext<AuthState>({
  user: null,
  linkedAccounts: [],
  isLoading: true,
  isAuthenticated: false,
  login: () => {},
  logout: () => {},
  refresh: async () => {},
});

export function useAuth(): AuthState {
  return useContext(AuthContext);
}

// ── Provider ───────────────────────────────────────────────────────────────

export function AuthProvider({ children }: { children: ReactNode }) {
  const [user, setUser] = useState<AuthUser | null>(null);
  const [linkedAccounts, setLinkedAccounts] = useState<LinkedAccount[]>([]);
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
        } else {
          setUser(null);
          setLinkedAccounts([]);
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

  return (
    <AuthContext.Provider
      value={{
        user,
        linkedAccounts,
        isLoading,
        isAuthenticated: !!user,
        login,
        logout,
        refresh,
      }}
    >
      {children}
    </AuthContext.Provider>
  );
}
