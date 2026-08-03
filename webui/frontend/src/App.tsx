import { Routes, Route, Navigate } from 'react-router-dom';
import { AuthProvider } from './lib/auth';
import Layout from './components/Layout';
import ErrorBoundary from './components/ErrorBoundary';
import PWAUpdateToast from './components/PWAUpdateToast';
import ProtectedRoute, { AdminRoute } from './components/ProtectedRoute';
import LoginPage from './components/LoginPage';
import RegisterPage from './components/RegisterPage';
import AuthCallback from './pages/AuthCallback';
import Dashboard from './pages/Dashboard';
import Agents from './pages/Agents';
import Conversations from './pages/Conversations';
import Logs from './pages/Logs';
import Settings from './pages/Settings';
import Plugins from './pages/Plugins';

export default function App() {
  return (
    <AuthProvider>
      <PWAUpdateToast />
      <Routes>
        {/* Public routes — no sidebar, no auth guard */}
        <Route path="/login" element={<LoginPage />} />
        <Route path="/register" element={<RegisterPage />} />
        <Route path="/auth/callback" element={<AuthCallback />} />

        {/* Protected routes — require authentication */}
        <Route
          path="*"
          element={
            <ProtectedRoute>
              <Layout>
                <ErrorBoundary>
                  <Routes>
                    {/* Everyone can see dashboard and settings */}
                    <Route path="/" element={<Dashboard />} />
                    <Route path="/settings" element={<Settings />} />

                    {/* Admin-only */}
                    <Route path="/agents" element={<AdminRoute><Agents /></AdminRoute>} />
                    <Route path="/conversations" element={<AdminRoute><Conversations /></AdminRoute>} />
                    <Route path="/logs" element={<AdminRoute><Logs /></AdminRoute>} />
                    <Route path="/plugins" element={<AdminRoute><Plugins /></AdminRoute>} />

                    <Route path="*" element={<Navigate to="/" replace />} />
                  </Routes>
                </ErrorBoundary>
              </Layout>
            </ProtectedRoute>
          }
        />
      </Routes>
    </AuthProvider>
  );
}
