# aestival — 绯英

AI-powered QQ bot with Agent capabilities, built on C++20 + Boost.Asio/Beast.

## Architecture

```
app/           entry point, adapters (main, llm, im, pch)
core/
  agent/       LLM agent loop, tool calling
  config/      bot_config loader + JSON schema
  context/     conversation storage (SQLite), summarization
  policy/      safety layer (rate limit, injection detection, SSRF)
  plugin/      plugin system, search commands
  search/      multi-platform web search backend
  self_iteration/  Claude Code powered self-improvement
  command/     system commands (usage, mode, clear, stop)
  util/        logging, encode utils, token counter, console API
platform/
  llm/         DeepSeek / OpenAI API clients
  http/        connection pool
  websocket/   WS client + URI parser
  qq/          QQ Bot protocol (session, auth, dispatch, guild API)
workspace/     AI persona files (PROMPT, SOUL, AGENTS, TOOLS, etc.)
```

## Build

### Prerequisites

- CMake >= 3.20
- C++20 compiler (MSVC or Clang/GCC)
- [vcpkg](https://github.com/microsoft/vcpkg)

### Dependencies (all via vcpkg)

```
boost-asio, boost-beast, boost-locale, boost-regex,
openssl, nlohmann-json, magic-enum, sqlite3
```

```bash
git clone <repo> aestival && cd aestival
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

Output: `bin/aestival.exe`

## Configuration

`config/bot_config.json`:

```json
{
  "qq": { "app_id": "", "app_secret": "" },
  "deepseek": {
    "api_key": "",
    "model": "deepseek-chat",
    "user_token": "",
    "waf_cookie": ""
  },
  "openai": { "api_key": "", "model": "gpt-4o", "base_url": "..." },
  "llm_provider": "deepseek",
  "admins": ["<user_hash_id>"],
  "mode": "agent",
  "verify_tls": true
}
```

## Run

```bash
# QQ mode (connects to QQ Bot WebSocket)
aestival.exe

# Console mode (stdin/stdout, no QQ credentials needed)
aestival.exe --console
```

## System Commands

| Command | Description | Permission |
|---------|-------------|------------|
| `switch mode` | Show current mode | anyone |
| `switch mode agent/plugin` | Switch mode | admin |
| `clear` | Clear conversation history | anyone |
| `usage` | DeepSeek token / cost stats | anyone |
| `delete database` | Delete SQLite DB | admin |
| `stop` | Shut down bot | admin |
| `self-iterate` | Run self-improvement cycle | admin |
| `self-iterate dry-run` | Evaluate only, no changes | admin |

## Search Commands

- `/bilibili <query>`
- `/v2ex <query>`
- `/github <query>`
- `/twitter <query>`
- `fetch <URL>`

## Safety

- SSRF protection (blocks private/localhost/multicast IPs, IPv4 + IPv6)
- Command injection prevention (shell metacharacter escaping)
- Rate limiting (per-user, per-minute)
- Daily token budget
- Prompt injection detection
- Unicode spam detection

## License

MIT
