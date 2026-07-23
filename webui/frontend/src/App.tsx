import { Routes, Route, Navigate } from 'react-router-dom';
import { useAuth } from './lib/auth';
import Layout from './components/Layout';
import Dashboard from './pages/Dashboard';
import Agents from './pages/Agents';
import Conversations from './pages/Conversations';
import Logs from './pages/Logs';
import Settings from './pages/Settings';
import Login from './pages/Login';

export default function App() {
  const { user, loading } = useAuth();

  if (loading) {
    return (
      <div className="flex h-screen items-center justify-center">
        <div className="animate-spin h-8 w-8 border-2 border-indigo-400 border-t-transparent rounded-full" />
      </div>
    );
  }

  if (!user) {
    return <Login />;
  }

  return (
    <Layout>
      <Routes>
        <Route path="/" element={<Dashboard />} />
        <Route path="/agents" element={<Agents />} />
        <Route path="/conversations" element={<Conversations />} />
        <Route path="/logs" element={<Logs />} />
        <Route path="/settings" element={<Settings />} />
        <Route path="*" element={<Navigate to="/" replace />} />
      </Routes>
    </Layout>
  );
}
