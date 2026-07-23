# CLAUDE.md

本文件为 Claude Code（claude.ai/code）在本仓库中工作时提供指导。

## 行为约束

- **遵循现代标准**：严格遵循 C++20 及项目已确立的编码规范，确保语义清晰、代码可读。
- **优先使用已有依赖**：实现需求时，优先选用 `vcpkg.json` 中已有的库（Boost.Asio/Beast、nlohmann/json、magic_enum、SQLite3、OpenSSL），避免引入新依赖。
- **可推荐合适开源库**：审查代码时，若现有依赖无法满足需求，可以推荐生态契合度高、许可证兼容的开源库。
- **操作前确认**：执行任何有副作用的操作（删除文件、修改配置、推送代码、重启服务等）之前，必须系统性地向用户说明意图并等待确认。
- **命令意图透明**：执行脚本或 shell 命令前，向用户解释该命令的目的和预期效果。
- **错误兜底**：当用户指出错误时，优先分析根因、提供修复方案和预防措施，避免同类错误再次发生。
- **禁止 AI 署名**：git commit 信息中**严格禁止**包含 `Co-Authored-By`、`Generated with Claude Code` 等 AI 相关署名。

## 构建与运行

### 初次配置

```bash
git clone <repo> aestival && cd aestival
# vcpkg 自动引导，无需手动安装
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
```

如果已设置 `VCPKG_ROOT` 环境变量，CMakeLists.txt 中的自动引导逻辑会直接跳过，直接使用工具链。

### 编译

```bash
cmake --build build --config Release --parallel
# 输出: bin/aestival.exe (Windows) 或 bin/aestival (Linux/macOS)
```

`CMakePresets.json` 中提供了 Visual Studio 的构建预设。

**CI 矩阵：** Windows（MSVC Debug/Release、ClangCL Release）、Linux（GCC-14 Release、Clang Release）、macOS（apple-clang Release）。CI 使用 CMake 4.3.3，本地 CMake ≥ 3.20 即可。

### 运行

```bash
# 控制台模式（stdin/stdout，无需 QQ 凭据）
aestival --console

# QQ 模式（连接 QQ Bot WebSocket，需要配置文件）
aestival
```

### 部署

```bash
# 将最新 CI 制品部署到生产服务器 (122.51.129.97)
./deploy.sh                  # 从当前分支
./deploy.sh --branch main    # 从其他分支
./deploy.sh --restart        # 部署后重启服务

# PR 合并后清理：将当前分支重置为 origin/main 并强制推送
./deploy.sh --sync
```

CI 在推送 `shinshi` 分支时自动部署（仅 Linux GCC Release），通过 SSH 完成。

### 依赖（vcpkg）

`vcpkg.json` 清单模式。核心依赖：`boost-asio`、`boost-beast`、`boost-locale`、`boost-regex`、`openssl`、`nlohmann-json`、`magic-enum`、`sqlite3`。全部由 vcpkg 在配置阶段解析。

## 架构

项目是一个单二进制 C++20 应用，分为四层：

### 第一层：`app/` — 入口与组装

- [main.cpp](app/main.cpp) — 解析 `--console` 参数，加载配置，注册插件，启动 QQ 会话或控制台循环
- [llm_adapter.cpp](app/llm_adapter.cpp) — 工厂函数 `make_model_client()`，根据配置选择 DeepSeek 或 OpenAI；两者均为实现 `model_client` 抽象接口的薄适配层
- [im_adapter.cpp](app/im_adapter.cpp) — 将 QQ WebSocket 分发事件接入 agent_controller
- [stdafx.h](app/stdafx.h) — **预编译头**：每个 `.cpp` 文件首先包含此头文件。它聚合了所有 STL、Boost.Asio/Beast、nlohmann/json、magic_enum、OpenSSL 和 sqlite3 头文件。CMake 使用 `target_precompile_headers`——**不要**在单个 `.cpp` 文件中新增重型头文件，应添加到 `stdafx.h` 中

### 第二层：`core/` — 业务逻辑

**核心抽象（位于 `core/include/`）：**

| 接口 | 职责 |
|-----------|------|
| `bot_messaging` | 抽象消息后端——由 QQ 适配器和 `console_api` 实现 |
| `model_client` | 抽象 LLM——由 `deepseek_adapter` 和 `openai_adapter` 实现 |
| `chat_storage_backend` | 抽象会话存储——由 `sqlite_backend` 实现 |
| `tool_provider` | 暴露 function-calling 工具的混入接口——由插件实现 |
| `plugin` | 继承自 `tool_provider`；具有 `can_handle()`、`handle()`、优先级、能力位 |

**核心模块：**

- **`agent_controller`** — 总调度器。在 `agent` 模式下运行 LLM 驱动的工具调用循环（`tool_loop`）。在 `plugin` 模式下通过 `plugin_manager` 分发给已注册插件。处理系统命令（`switch mode`、`clear`、`usage`、`stop`、`self-iterate`）
- **`plugin_manager`** — 按优先级排列插件，分发消息，捕获每个插件的异常，确保单个异常插件不会导致机器人崩溃
- **`tool_registry`** — 从所有 `tool_provider` 收集 `tool_definition` 对象，构建 LLM function calling 所需的 JSON `tools` 数组。按名称查找并路由工具执行
- **`worker_pool`** — 每个会话一个线程。同一会话的消息按序处理；不同会话并行执行。接受 `processor` 回调
- **`chat_context_manager`** — 封装 `chat_storage_backend`，提供追加/摘要/清空操作。上下文过长时通过 LLM 进行摘要
- **`policy_engine`** — **确定性安全层**（无模型调用）。四个检查点：`check_input`（提示注入/滥用扫描）、`check_llm_call`（每用户速率限制 + 每日令牌预算）、`filter_output`（LLM 输出安全过滤）、`sanitize_reply`（最终截断/转义）
- **`self_iteration_engine`** — 调用 `claude -p`（Claude Code CLI 单次模式）作为外部进程。两个阶段：评估（按语气/准确性/完整性/效率四项维度对最近对话打分）和改进（对 `workspace/*.md` 提出文件编辑建议）。仅修改 `workspace/` 和 `bot_config.json`——永不触碰 C++ 源码
- **`agent_reach_client`** — 封装 CLI 工具用于 Web 搜索（Exa 通过 mcporter、Jina Reader 抓取页面、bili-cli、V2EX/GitHub/Wikipedia/Twitter API）。同步调用；探测 PATH 检查可用后端
- **`system_command_handler`** — 纯逻辑，无线程/所有权。从 agent_controller 中抽离以提高可测试性
- **`bot_config`** — 通过 nlohmann/json 从 `config/bot_config.json` 加载。支持 QQ 凭据、DeepSeek/OpenAI 密钥、管理员用户 ID、workspace 路径、安全阈值、自迭代设置

### 第三层：`platform/` — 基础设施

- **`platform/llm/`** — DeepSeek 和 OpenAI chat completion API 客户端。每次调用通过 Beast 进行同步 HTTP(S) 请求
- **`platform/http/`** — HTTP(S) 请求连接池。包含清洗工具和 HTTP 辅助头文件
- **`platform/ws/`** — QQ 传输层使用的 WebSocket 客户端 + URI 解析器
- **`platform/qq/`** — QQ Bot 协议实现：
  - `session` — WebSocket 生命周期（连接、心跳、重连、令牌刷新）。在独立线程上运行专属 `io_context`。使用 Boost.Asio 协程（`boost::asio::awaitable`）
  - `auth` — 从 QQ API 获取 access_token 和 gateway URL
  - `transport` — WebSocket 连接、发送、接收循环
  - `dispatch` — 解析 QQ 事件负载，路由到类型化处理函数（C2C 消息、群消息、频道消息等）
  - `api` / `api_guild` / `api_messaging` — 用于发送消息、管理频道等的 REST API 调用

### 第四层：`workspace/` — AI 人设

定义机器人性格（"绯英"——崩坏星穹铁道角色）的 Markdown 文件。启动时加载，注入为 agent 模式的 system prompt。这些文件随二进制文件一起部署，也是**自迭代唯一允许修改的文件**（外加 `bot_config.json`）。

- `SOUL.md` — 角色背景、性格、语气风格
- `PROMPT.md` — 核心行为提示词
- `AGENTS.md`、`TOOLS.md`、`GROUP_RULES.md` — agent 行为规范
- `IDENTITY.md`、`MEMORY.md`、`USER.md` — 上下文文件

## 关键模式

### 抽象接口 + 适配器

`bot_messaging` 和 `model_client` 使用相同的模式：纯虚接口定义在 `core/include/` 中，具体实现在 `app/`（适配器）或 `platform/` 中。这使得控制台模式可以无缝替换 `console_api` 代替 QQ 后端，无需修改 `agent_controller`。

### 预编译头

所有 `.cpp` 文件首先 `#include "stdafx.h"`。PCH 由 CMake 的 `target_precompile_headers` 管理。向多个编译单元添加新的重型头文件？放入 `stdafx.h`。

### 运行时模式

定义在 [runtime_mode.h](core/include/runtime_mode.h)：`plugin`（消息→插件链→回复）和 `agent`（消息→LLM 工具循环→回复）。通过 `switch mode` 系统命令切换。默认值在 `bot_config.json` 中设置。

### 线程模型

- QQ session：一个独立线程运行 `io_context::run()`，配合 Boost.Asio 协程
- Worker pool：每个会话一个线程——同一会话串行执行，不同会话并行执行
- `agent_controller::state_mutex_` 保护模式切换
- Policy engine：`rate_mutex_` 用于每用户速率限制状态，原子变量用于令牌预算

### 配置

[bot_config.h](core/config/bot_config.h) 定义一个纯结构体。[bot_config.cpp](core/config/bot_config.cpp) 从 JSON 加载。路径在启动时相对于可执行文件目录解析（参见 main.cpp 中的 `exe_dir()`）。仓库中的配置文件包含真实令牌——切勿在源码中硬编码密钥。

### 安全（确定性规则，非 LLM 判断）

`policy_engine` 中的所有检查均为基于规则的 regex/CIDR/速率限制检查。它们在 LLM **之下**运行——快速、可预测、可审计。策略覆盖：SSRF（同时阻止 IPv4 和 IPv6 的私有/本地/多播 IP）、命令注入（shell 元字符转义）、提示注入检测、Unicode 垃圾消息检测、每用户速率限制、每日令牌预算。

### 工具调用

插件继承自 `tool_provider` 并覆写 `get_tools()` + `execute_tool()`。`agent_controller` 调用 `build_tools()` 从 `tool_registry`（从已注册插件填充）收集工具定义。在 agent 工具循环中，LLM 请求的工具调用通过 `tool_registry::execute()` 分发执行。

### 自迭代

通过 `self-iterate` 命令手动调用，或在启用时按间隔自动触发。以子进程方式运行 Claude Code CLI（`claude -p`）——需要 `claude` 在 PATH 中。阶段：从 SQLite 采集对话样本→Claude 评估→判断分数是否需要改进→制定文件编辑计划→应用编辑 + git commit。C++ 源码文件永远不会被修改。
