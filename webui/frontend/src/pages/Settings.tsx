export default function Settings() {
  return (
    <div>
      <h2 className="text-xl font-bold mb-6">设置</h2>

      <div className="bg-gray-900 rounded-lg border border-gray-800 p-6 max-w-md space-y-4">
        <div>
          <label className="text-sm text-gray-400">模式</label>
          <div className="text-lg font-medium">本地管理</div>
        </div>
        <div>
          <label className="text-sm text-gray-400">环境</label>
          <div className="text-sm">浏览器</div>
        </div>
      </div>

      <div className="mt-8 bg-gray-900 rounded-lg border border-gray-800 p-6 max-w-md">
        <h3 className="font-semibold mb-2">关于</h3>
        <p className="text-sm text-gray-400">
          aestival 管理面板 — 多 Agent Bot 管理、对话审查、日志查看。
        </p>
        <p className="text-xs text-gray-600 mt-2">v1.0.0</p>
      </div>
    </div>
  );
}
