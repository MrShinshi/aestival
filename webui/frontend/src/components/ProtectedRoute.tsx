/**
 * Route guards.
 *
 * ProtectedRoute  — redirects to /login when not authenticated.
 * AdminRoute      — checks isAdmin after authentication; redirects to / if not admin.
 */

import { Navigate } from 'react-router-dom';
import { useAuth } from '../lib/auth';
import type { ReactNode } from 'react';

export default function ProtectedRoute({ children }: { children: ReactNode }) {
  const { isAuthenticated, isLoading } = useAuth();

  if (isLoading) {
    return (
      <div className="flex items-center justify-center h-screen bg-[#0f0d1e]">
        <div className="text-center">
          <div className="animate-spin h-8 w-8 border-2 border-indigo-500 border-t-transparent rounded-full mx-auto mb-3" />
          <p className="text-gray-400 text-sm">加载中…</p>
        </div>
      </div>
    );
  }

  if (!isAuthenticated) {
    return <Navigate to="/login" replace />;
  }

  return <>{children}</>;
}

export function AdminRoute({ children }: { children: ReactNode }) {
  const { isAdmin, isLoading } = useAuth();

  if (isLoading) {
    return (
      <div className="flex items-center justify-center h-screen bg-[#0f0d1e]">
        <p className="text-gray-400 text-sm">加载中…</p>
      </div>
    );
  }

  if (!isAdmin) {
    return <Navigate to="/" replace />;
  }

  return <>{children}</>;
}
