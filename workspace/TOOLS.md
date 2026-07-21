# 可用命令参考

## 运行命令

使用 `shell` 工具执行以下命令。工具接受一个参数 `command`（完整的 shell 命令字符串）。

## 网页全文阅读

```bash
curl -s --max-time 15 "https://r.jina.ai/URL"
```
将任意网页转为可读文本。用户给链接时使用。

## B站 (bilibili)

本机已登录，可以访问登录态数据。

```bash
# 搜索视频
bili search "关键词" --type video -n 5

# 热门排行
bili hot -n 5

# 我的关注列表
bili following
# 翻页 (每页20条，第2页)
bili following --page 2
# 或者以 YAML 输出（对 AI 更友好）
bili following --yaml

# 我的收藏夹
bili favorites

# 我的观看历史
bili history -n 10

# UP 主资料
bili user UID_OR_NAME

# UP 主的视频列表
bili user-videos UID_OR_NAME -n 10

# 当前登录用户
bili whoami

# 全站排行榜
bili rank

# 获取视频信息（B站链接请用此命令，不要用 Jina Reader）
bili video BV号_OR_URL
```
B站链接请用 `bili video` 直接解析，不要走 curl/Jina Reader——B站有反爬，Jina 只能拿到标题和简介。

当用户提到"我的关注""我关注了谁""关注列表"时，用 `bili following`。
当用户提到"我的收藏""收藏夹"时，用 `bili favorites`。

**翻页**：`bili following`、`bili favorites`、`bili history` 均支持 `--page N`（默认每页20条）。
用户要"继续""还有吗""下一页"时，使用 `--page 2`、`--page 3` 等重新查询。
`--yaml` 标志可获得对 AI Agent 更友好的结构化输出。

## V2EX

```bash
# 热门主题（综合查看）
curl -s --max-time 10 "https://www.v2ex.com/api/topics/hot.json" -H "User-Agent: agent-reach/1.0"

# 按节点/话题查询（如 Python、Rust、AI）
curl -s --max-time 10 "https://www.v2ex.com/api/topics/show.json?node_name=节点名&page=1" -H "User-Agent: agent-reach/1.0"
```
返回 JSON 数组（title, node.title, replies, url）。limit 5 条足够。用户问特定话题时优先用节点 API 而非 hot.json。

## GitHub

```bash
# 仓库搜索 (建议 --limit 3-5)
gh search repos "关键词" --sort stars --limit 3 --json name,description,stargazersCount,url
```
搜索语法提示：搜特定语言项目用 `language:语言名`（如 `gh search repos "language:rust machine learning" --sort stars --limit 5 --json ...`）。

## Twitter/X

```bash
# 搜索推文
twitter search "关键词" -n 5
```

## 天气查询

```bash
# 三天预报（默认格式，推荐）
curl -s --max-time 15 "https://wttr.in/城市名?lang=zh"

# 仅当天简况
curl -s --max-time 15 "https://wttr.in/城市名?lang=zh&format=3"
```
城市名用中文（如"北京""东京"）即可。默认格式含今天+明天+后天三天预报，适合"明天/后天会下雨吗"类问题。

## Exa 语义搜索

```bash
mcporter call 'exa.web_search_exa(query: "关键词", numResults: 5)' --output json
```
需要安装 mcporter CLI。语义搜索质量最高，推荐首选。

## Wikipedia 搜索（零配置后备）

```bash
curl -s --max-time 10 "https://en.wikipedia.org/w/api.php?action=query&list=search&srsearch=URL_ENCODED_KEYWORD&format=json&srlimit=5"
```
无需安装任何工具，curl 即可用。适合事实查询、人物、事件。当 Exa 不可用时自动作为后备。关键词用英文更佳（中文也可搜索但结果较少）。

## 规则

- weather/新闻/排行/热搜 → shell 工具搜索
- 我的/关注/收藏/历史 → shell 工具查询（B站已登录）
- 编程/常识 → 直接回答，不调工具
- chat/角色扮演 → 不调工具
- 搜索结果为空 → 诚实告诉用户，不要重复搜同样的关键词
- **回复时完整列出工具返回的所有条目，不要省略。如果只返回了 5 条就说 5 条，不要说 10 条或 20 条**
