import { useState, useEffect, useCallback } from 'react';

interface AuthState {
  user: string | null;
  token: string | null;
  loading: boolean;
  login: () => void;
  logout: () => void;
}

let cached: { user: string; token: string } | null = null;

export function useAuth(): AuthState {
  const [user, setUser] = useState<string | null>(null);
  const [token, setToken] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    // Check existing session
    fetch('/api/ui/auth/me', { credentials: 'include' })
      .then(r => r.ok ? r.json() : null)
      .then(data => {
        if (data?.user) {
          setUser(data.user);
          // Fetch JWT token
          return fetch('/api/ui/auth/token', { credentials: 'include' }).then(r => r.json());
        }
        return null;
      })
      .then(tokenData => {
        if (tokenData?.token) {
          setToken(tokenData.token);
          cached = { user: tokenData.user || '', token: tokenData.token };
        }
      })
      .catch(() => {})
      .finally(() => setLoading(false));
  }, []);

  const login = useCallback(() => {
    window.location.href = '/api/ui/auth/github';
  }, []);

  const logout = useCallback(async () => {
    await fetch('/api/ui/auth/logout', { method: 'POST', credentials: 'include' });
    setUser(null);
    setToken(null);
    cached = null;
  }, []);

  return { user, token, loading, login, logout };
}

export function getStoredToken(): string | null {
  return cached?.token || null;
}
