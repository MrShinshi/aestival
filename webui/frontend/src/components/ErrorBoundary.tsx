import { Component, type ReactNode } from 'react';

interface Props {
  children: ReactNode;
  fallback?: ReactNode;
}

interface State {
  error: Error | null;
}

export default class ErrorBoundary extends Component<Props, State> {
  state: State = { error: null };

  static getDerivedStateFromError(error: Error): State {
    return { error };
  }

  componentDidCatch(error: Error, info: { componentStack: string }) {
    console.error('[ErrorBoundary]', error, info.componentStack);
  }

  render() {
    if (this.state.error) {
      return this.props.fallback || (
        <div className="flex items-center justify-center h-full">
          <div className="bg-red-900/50 border border-red-800 rounded-lg p-6 max-w-md text-center">
            <h2 className="text-lg font-bold text-red-300 mb-2">Something went wrong</h2>
            <p className="text-sm text-red-400 mb-4">{this.state.error.message}</p>
            <button
              onClick={() => this.setState({ error: null })}
              className="px-3 py-1.5 bg-red-800 hover:bg-red-700 text-red-200 text-sm rounded transition-colors"
            >
              Retry
            </button>
          </div>
        </div>
      );
    }
    return this.props.children;
  }
}
