import { useEffect, useRef } from 'react';
import { RefreshCw, Wifi, X } from 'lucide-react';
import { useRegisterSW } from 'virtual:pwa-register/react';

/** How often to poll the service worker for a newer build (while the tab is open). */
const UPDATE_CHECK_MS = 60 * 60 * 1000; // 1 hour

/**
 * PWA lifecycle toast:
 *  - announces the app is ready for offline / install,
 *  - prompts the user to reload when a new version of the app shell is available.
 */
export default function PWAUpdateToast() {
  const pollRef = useRef<number | undefined>(undefined);

  const {
    offlineReady: [offlineReady, setOfflineReady],
    needRefresh: [needRefresh, setNeedRefresh],
    updateServiceWorker,
  } = useRegisterSW({
    onRegisteredSW(_swUrl, registration) {
      if (!registration) return;
      // Make sure we don't stack multiple intervals across re-registrations.
      if (pollRef.current !== undefined) window.clearInterval(pollRef.current);
      pollRef.current = window.setInterval(() => {
        registration.update();
      }, UPDATE_CHECK_MS);
    },
    onRegisterError(error) {
      console.warn('[pwa] service worker 注册失败', error);
    },
  });

  useEffect(() => {
    return () => {
      if (pollRef.current !== undefined) window.clearInterval(pollRef.current);
    };
  }, []);

  if (!offlineReady && !needRefresh) return null;

  return (
    <div className="pointer-events-none fixed inset-x-0 bottom-4 z-50 flex justify-center px-4">
      <div className="pointer-events-auto flex items-center gap-3 rounded-xl border border-gray-700 bg-gray-900/95 px-4 py-3 shadow-xl shadow-black/40 backdrop-blur">
        {needRefresh ? (
          <>
            <RefreshCw size={18} className="shrink-0 text-indigo-400" aria-hidden="true" />
            <p className="text-sm text-gray-200">检测到新版本，刷新后生效</p>
            <button
              onClick={() => updateServiceWorker(true)}
              className="ml-1 rounded-lg bg-indigo-500 px-3 py-1.5 text-sm font-medium text-white transition-colors hover:bg-indigo-400"
            >
              立即刷新
            </button>
            <button
              onClick={() => setNeedRefresh(false)}
              className="rounded p-1 text-gray-400 transition-colors hover:text-gray-200"
              aria-label="关闭"
            >
              <X size={16} aria-hidden="true" />
            </button>
          </>
        ) : (
          <>
            <Wifi size={18} className="shrink-0 text-emerald-400" aria-hidden="true" />
            <p className="text-sm text-gray-200">应用已支持离线使用，可添加到主屏幕</p>
            <button
              onClick={() => setOfflineReady(false)}
              className="rounded p-1 text-gray-400 transition-colors hover:text-gray-200"
              aria-label="关闭"
            >
              <X size={16} aria-hidden="true" />
            </button>
          </>
        )}
      </div>
    </div>
  );
}
