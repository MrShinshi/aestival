# aestival — 绯英

AI-powered QQ bot with multi-Agent capabilities and a Web management UI.
Built on C++20 + Boost.Asio/Beast, with a React 19 + Express frontend.

## Features

- **多 Agent 架构** — `agent_registry` 统一管理 Agent 生命周期（启停/增删/持久化），每会话独立线程，互不阻塞
- **LLM 抽象** — DeepSeek / OpenAI 双后端，统一 `model_client` 接口
- **MCP 协议支持** — JSON-RPC 2.0 over stdio，工具通过 `tools/list` 自动发现
- **插件系统** — 用户级插件配置（SQLite 持久化，即时生效），工具注册 + 分发
- **管理 API** — Boost.Beast HTTP（仅监听 127.0.0.1）+ HMAC-SHA256 JWT，Agent/日志/对话/Token 端点
- **Web 管理面板** — React 19 + TypeScript + Vite + Tailwind CSS
  - GitHub / QQ OAuth + 用户名密码登录，管理员权限体系
  - 系统 + 进程 CPU/内存实时监控图表、Token 用量统计
  - 会话审查、日志查看、Agent 管理、插件开关
- **PWA 支持** — 可安装到桌面/主屏幕，Service Worker 离线缓存应用外壳，新版本更新提示
- **自迭代引擎** — 调用 Claude Code CLI 评估并改进 `workspace/` 人设文件
- **确定性安全层** — SSRF / 命令注入 / 提示注入检测、限流、每日 token 预算

## Repository Layout

```
app/          入口与组装（main, llm/im adapter, PCH）
core/
  include/    核心抽象接口（model_client, bot_messaging, plugin, tool_provider…）
  src/        agent_controller, agent_registry, management_api, mcp_client,
              plugin_manager, policy_engine, system_monitor, self_iteration…
platform/
  llm/        DeepSeek / OpenAI API 客户端
  http/       HTTP(S) 连接池
  ws/         WebSocket 客户端
  qq/         QQ Bot 协议（session/auth/transport/dispatch/api）
workspace/    AI 人设文件（SOUL/PROMPT/AGENTS/TOOLS/…），随二进制部署
webui/
  backend/    Express + TypeScript 后端（OAuth/JWT、SQLite、bot API 代理）
  frontend/   React 19 + Vite 前端（含 PWA）
  shared/     前后端共享类型与校验
  deploy/     nginx 反代配置 + systemd 服务文件
config/       bot_config.json（含真实凭据，不提交）
tests/        测试
```

## Build

### C++ 核心

```bash
# vcpkg 自动引导（或预置 VCPKG_ROOT）
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release --parallel
# 输出: bin/aestival(.exe)
```

依赖（vcpkg 清单模式）：`boost-asio` `boost-beast` `boost-locale` `boost-regex`
`openssl` `nlohmann-json` `magic-enum` `sqlite3`

### Web UI

```bash
cd webui/frontend && npm install && npm run build     # 前端 → dist/
cd webui/backend && npm install && npx tsc             # 后端 → dist/
```

## Run

```bash
# QQ 模式（连接 QQ Bot WebSocket，需配置）
aestival

# 控制台模式（stdin/stdout，无需 QQ 凭据）
aestival --console

# Web UI
node webui/backend/dist/index.js   # 后端 :3000；开发时前端 vite :5173 走代理
```

## Configuration

`config/bot_config.json` — 多 Agent 数组格式（兼容旧单 Agent 格式自动转换）：

```jsonc
{
  "agents": [
    {
      "id": "default",
      "name": "绯英",
      "qq": { "app_id": "", "app_secret": "" },
      "llm": { "provider": "deepseek", "model": "deepseek-chat", "api_key": "" },
      "mode": "agent",
      "storage_dir": "contexts/default",
      "admin_ids": []
    }
  ],
  "admins": ["<user_hash_id>"],
  "management_api": { "port": 9090, "secret": "" }
}
```

配置加载时密钥字段以 `***` 脱敏落盘；`config/bot_config.json` 含真实令牌，**永不提交**（本地通过环境变量或独立 secrets 文件注入）。

## System Commands

| 命令 | 说明 | 权限 |
|------|------|------|
| `switch mode agent/plugin` | 切换运行模式 | admin |
| `clear` | 清空当前会话上下文 | anyone |
| `usage` | DeepSeek 用量/费用 + 本地 30 天统计 | anyone |
| `delete database` | 删除 SQLite 数据库 | admin |
| `stop` | 关闭机器人 | admin |
| `self-iterate [dry-run]` | 运行自迭代（评估/改进） | admin |
| `agent list/start/stop/remove` | Agent 生命周期管理 | admin |
| `plugin list/enable/disable/info` | 插件配置管理 | admin |

## Deploy

- **CI**：推送 `shinshi` 分支自动构建（仅 Linux GCC Release）并 SSH 部署；WebUI 独立构建部署
- **`.deploy_config`**（仓库根，永不提交）：定义部署目标与主机密钥
  ```bash
  AESTIVAL_TARGET=user@<server-ip>
  AESTIVAL_HOST_KEY="ssh-ed25519 AAAA..."
  ```
- **deploy.sh**：`--branch` 选择分支、`--restart` 部署后重启、`--sync` 同步本地+远程
- **WebUI**：nginx 反代（`webui/deploy/nginx-aestival.conf`）+ systemd（`aestival-webui.service`）。**PWA 的安装/离线能力要求 HTTPS**——纯 IP + HTTP 环境下不可用

## Safety

`policy_engine` 为确定性规则层（无 LLM 判断），在模型之下运行：SSRF 防护（IPv4/IPv6 私网/多播拦截）、命令注入转义、提示注入检测、Unicode 垃圾消息检测、每用户速率限制、每日 token 预算。WebUI 侧另有 JWT 鉴权、管理员权限、路径遍历防护与输入校验。

## License

MIT
