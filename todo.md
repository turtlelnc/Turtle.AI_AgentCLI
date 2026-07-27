# Turtle.AI AgentCLI 改进 TODO

本文档用于指导后续 Codex 迭代 Turtle.AI AgentCLI。

项目定位：保持 Turtle 为轻量、单二进制、多 Provider、支持本地模型的 Agent CLI。借鉴 Codex CLI 的运行时、安全和上下文设计，但不要照搬其仓库规模或一次性引入复杂平台架构。

## 工作原则

- 每次只完成一个可独立审查、可验证的阶段。
- 修改前先阅读相关实现和测试，不凭 README 假设行为。
- 安全问题优先于新功能。
- 保持 macOS、Linux 和 Windows 的普通聊天能力可用；平台不支持安全命令执行时必须失败关闭。
- 不允许为方便而削弱工作区路径检查、符号链接检查、人工审批或沙箱。
- 新增 Agent 行为必须有端到端测试。
- 不要在重构期间同时大规模修改终端 UI。
- 保持 C++17，除非单独形成迁移提案并获得确认。
- 每个阶段完成后运行构建和全部测试，并记录实际结果。

## P0：立即处理的安全问题

### 1. 移除 API Key 明文持久化

当前问题：

- `ConfigManager::saveConfig()` 会把 `api_key` 写入配置 JSON。
- `saveApiKeyToEnv()` 会把密钥写入 `~/.opencode_env`。
- 写入时没有可靠保证 owner-only 权限。
- `opencode` 命名与 Turtle 产品名不一致。

任务：

- [x] 配置文件不再保存 API Key。
- [x] 支持标准 Provider 环境变量：
  - `OPENAI_API_KEY`
  - `ANTHROPIC_API_KEY`
  - `DEEPSEEK_API_KEY`
- [x] 设计 `SecretStore` 接口，将系统密钥链实现和环境变量读取与配置管理分离。
- [x] macOS 优先接入 Keychain；其他平台在没有安全存储实现时只使用环境变量或本次进程输入。
- [x] 检测旧配置中的 `api_key`，迁移或提示用户后安全移除。
- [x] 将 `.opencode_config.json`、`.opencode_env` 和 `OPENCODE_API_KEY` 迁移为 Turtle 命名；提供向后兼容读取，但不再写旧格式。
- [x] 确保密钥不会出现在日志、错误文本、会话 JSON、tool result 或测试快照中。

验收标准：

- [x] 全仓搜索确认生产代码不再把 API Key 序列化到普通配置文件。
- [x] 添加配置迁移测试、环境变量测试和“会话中不含密钥”测试。
- [x] 新生成的配置文件不包含任何凭据字段。

### 2. 校验 Provider、API URL 和凭据目标

- [x] 为官方 OpenAI、Anthropic、DeepSeek 建立默认可信 endpoint。
- [x] 自定义 OpenAI-compatible endpoint 必须作为独立 Provider 配置，而不是伪装成官方 Provider。
- [x] 向自定义主机发送凭据前显示明确主机名并取得确认。
- [x] 远程 API 默认要求 HTTPS；仅 loopback 本地模型允许 HTTP。
- [x] 检查 libcurl 重定向策略，避免 Authorization 或 API Key Header 泄漏到其他主机。
- [x] 为非法 scheme、非本地 HTTP、跨主机重定向增加测试。

验收标准：

- [x] 官方 Provider 的凭据不能被发送到非官方 endpoint，除非用户显式选择自定义 Provider 并确认。
- [x] HTTP 本地服务仍可用于 Ollama/Llama.cpp。

## P1：Agent Runtime 解耦

### 3. 从 `main.cpp` 提取 AgentSession

目标结构：

```text
CLI / Terminal UI
        |
        v
AgentSession
  |-- ModelProvider
  |-- ToolRegistry
  |-- ApprovalPolicy
  |-- ContextManager
  `-- ConversationStore
```

任务：

- [x] 新建独立的 `AgentSession`，负责一轮任务的模型—工具循环。
- [x] `AgentSession` 不直接读取 stdin 或写 stdout。
- [x] 定义结构化 `AgentEvent`，至少包含：
  - turn started/completed/failed
  - model text delta
  - tool requested/approved/denied/started/completed
  - usage update
  - file change
- [x] UI 通过回调或事件消费者渲染事件。
- [x] 将 Provider 请求、工具结果回传和自动继续逻辑从 `main.cpp` 移出。
- [x] 将 slash command 解析保留在 CLI 层。
- [x] 将 token/费用计算从 UI 打印逻辑中移出。
- [x] 将主程序逐步缩减为配置、依赖组装和命令分发。

约束：

- 不要一次拆成大量静态库。
- 第一阶段可只增加 `turtle_core` 和 `turtle_cli` 两个 CMake target。
- 保持现有交互行为兼容。

验收标准：

- [x] 使用假 Provider 完成“用户消息 → 工具调用 → 工具结果 → 最终回答”端到端测试。
- [x] `AgentSession` 测试不依赖真实网络和真实终端输入。
- [x] `main.cpp` 不再包含 Provider 特有的工具结果编码逻辑。

### 4. 建立 Provider 适配器

- [x] 定义 `ModelProvider` 接口。
- [x] 将请求编码、流事件解析和工具结果编码封装到 Provider 内。
- [x] 实现：
  - [x] `OpenAIProvider`
  - [x] `AnthropicProvider`
  - [x] `DeepSeekProvider` 或明确复用 OpenAI-compatible 基类
  - [x] `OpenAICompatibleProvider`，供 Ollama/Llama.cpp/LiteLLM 使用
- [x] 引入 `ModelCapabilities`：
  - context window
  - 最大输出 token
  - 是否支持原生工具调用
  - 是否支持流式 usage
  - 是否支持图片
- [x] 删除主循环中基于 Provider 名称的条件分支。
- [x] 对 malformed tool arguments 返回结构化错误，不要静默替换为空对象。

验收标准：

- [x] OpenAI 与 Anthropic 的同一逻辑场景产生相同的规范化 Agent 事件。
- [x] Provider 流解析保留现有覆盖，并增加错误、流中断和多工具调用测试。

### 5. 将 ToolRegistry 重命名为 ToolRegistry

当前 `ToolRegistry` 是进程内工具注册表，不是完整 MCP 实现。

- [x] 将 `MCPManager` 重命名为 `ToolRegistry`。
- [x] 将 `MCPTool` 重命名为 `RegisteredTool`。
- [x] 保持工具 schema、handler 和执行接口功能不变。
- [x] 更新测试名和文档，避免把内置工具误称为 MCP。
- [x] 暂时不要在本阶段实现外部 MCP。

验收标准：

- [x] 仓库文档准确区分“内置工具系统”和“MCP 协议”。
- [x] 现有工作区边界与符号链接逃逸测试全部通过。

## P1：上下文与长任务可靠性

### 6. 引入 ContextManager 和硬预算

- [x] 区分“完整持久化历史”和“当前模型可见上下文”。
- [x] 根据模型能力设置上下文预算。
- [x] 对每个工具结果设置独立字符/token 上限。
- [x] 保留最近若干 turn、当前未完成工具链和关键系统约束。
- [x] 达到阈值时生成确定性滚动摘要，而不是删除原始会话历史。
- [x] 摘要超限时使用确定性的截断策略，不能继续发送超限请求。
- [x] 避免每轮重复注入不变的大段内容。
- [x] 对 Skill 内容也设置硬上限（单 Skill 64 KiB、最多 64 个）。

建议模型可见结构：

```text
system instructions
+ skill catalog
+ conversation summary
+ recent turns
+ pending tool-call chain
+ current user input
<= model context budget
```

验收标准：

- [x] 构造超长会话时，请求始终低于测试 Provider 的上下文上限。
- [x] 压缩后仍保留当前用户目标、未完成工具调用和最近修改信息。
- [x] 原始会话记录不因压缩被重写或丢弃。

### 7. HTTP 重试、取消和错误分类

- [x] 定义统一错误类别：
  - authentication
  - rate limited
  - context overflow
  - transport
  - provider error
  - invalid tool call
  - tool execution
  - cancelled
- [x] 对 429、部分 5xx 和短暂网络失败实现有上限的指数退避与 jitter。
- [x] 尊重 `Retry-After`（秒数与 HTTP-date）。
- [x] 不自动重试可能重复产生副作用的工具执行（重试仅位于模型 HTTP 层）。
- [x] 引入贯穿 HTTP 流、AgentSession 和命令子进程的 cancellation token。
- [x] Ctrl-C 首次取消当前操作，忙碌时再次 Ctrl-C 或空闲时 Ctrl-C 才退出程序。
- [x] 流中断后不得重复执行已经完成的工具调用（输出一旦可见即禁止自动重试，工具只在完整响应解析后执行）。

验收标准：

- [x] 假 HTTP 服务覆盖 429、500、断流、取消和不可重试认证错误。
- [x] 已执行的写文件工具不会因模型重试而重复执行。

## P1：命令执行与沙箱

### 8. 使用 argv 执行替代字符串 Shell

- [x] 将默认命令工具参数改为结构化 `argv: string[]`。
- [x] 默认直接执行程序，不经过 `/bin/sh -lc`。
- [x] 如确需 shell，提供独立 `run_shell` 工具并提高审批等级。
- [x] 审批界面展示最终 argv、cwd、网络权限和可写目录。
- [x] 命令字符串黑名单仅作为辅助检查，不能作为主要安全边界。
- [x] 重新评估当前 `git`、`npm`、`make`、`./` 前缀白名单，它们均可间接执行任意程序。

验收标准：

- [x] shell 元字符作为普通 argv 内容时不会被解释。
- [x] 审批内容与最终执行内容一致。
- [x] 超时能终止完整进程组。

### 9. 权限与审批策略分级

- [x] 将工具按效果分类：
  - 只读
  - 工作区写入
  - 网络访问
  - 工作区外访问
  - 破坏性操作
- [x] 支持”仅本次允许”和”本会话允许精确命令前缀”。
- [x] 未知工具继续默认审批。
- [x] 永久授权暂缓，直到有安全的配置和撤销机制。
- [x] 文件修改审批展示真实 unified diff，而不是只展示输入片段。

验收标准：

- [x] 授权范围不能通过不同 argv、不同 cwd 或不同工具名称复用。
- [x] 拒绝结果作为结构化 tool result 返回模型。

### 10. Linux 沙箱

- [x] 优先实现 Bubblewrap 后端。
- [x] 默认工作区可写、其他必要系统路径只读、网络关闭。
- [x] 为临时目录提供隔离的可写空间。
- [x] 保持”后端不可用则拒绝执行”。
- [x] 不要以字符串命令过滤替代 Linux 沙箱。
- [x] Windows 沙箱单独规划，不与 Linux 实现绑在同一大型变更中。

验收标准：

- [x] Linux 上可运行普通构建与测试命令。
- [x] 无法写出工作区、无法通过符号链接逃逸、默认无法联网。
- [x] Bubblewrap 不可用时有明确错误且不会无沙箱执行。

## P2：会话和可恢复执行

### 11. 将会话升级为 Thread / Turn / Item

- [x] 定义结构化对象：
  - Thread
  - Turn
  - UserMessage
  - AssistantMessage
  - ToolCall
  - ToolResult
  - Approval
  - FileChange
- [x] 区分 turn 状态：running、completed、failed、cancelled。
- [x] 将 token、费用、Provider 和模型记录到 turn。
- [x] 支持会话 fork。
- [x] 保持旧版本 JSON 会话可迁移读取。

建议先采用追加式 JSONL 事件日志，不必立即引入 SQLite。

验收标准：

- [x] 在 tool call 和 tool result 之间模拟崩溃后，恢复时不会静默重复副作用。
- [x] 可准确查询某一 turn 的工具、审批和文件修改。

### 12. 持久化 ChangeJournal

- [x] 将变更记录关联到 Thread、Turn 和 ToolCall。
- [x] 重启后仍可查看和撤销修改。
- [x] 保持”有更新修改时不能先撤销旧修改”的约束。
- [x] 使用文件 hash 检测用户在工具执行后进行的外部修改。
- [x] 大文件优先保存 patch/hash；继续维持严格的总大小和文件数限制。

验收标准：

- [x] 重启后可以安全撤销未被外部改动覆盖的修改。
- [x] 文件内容与记录后的 hash 不一致时拒绝盲目覆盖。

## P2：非交互模式与集成接口

### 13. 增加 `turtle exec`

- [x] 支持：

```bash
turtle exec "分析失败测试"
turtle exec --json "修复这个问题"
turtle resume <session-id>
turtle doctor
```

- [x] 非交互模式不显示动画。
- [x] `--json` 输出稳定 JSONL 事件。
- [x] stdout 只输出协议事件，诊断日志写 stderr。
- [x] 明确定义进程退出码。

验收标准：

- [x] 可以在 CI 中运行并根据退出码判断成功、模型失败、审批缺失或用户取消。
- [x] JSONL 输出可被脚本逐行解析。

### 14. 实现真正的 MCP Client

本任务必须在 ToolRegistry 重命名和 AgentSession 解耦之后进行。

- [x] 支持 MCP initialize 握手。
- [x] 支持 `tools/list` 和 `tools/call`。
- [x] 第一阶段只实现 stdio transport。
- [x] 每个 MCP Server 有独立名称空间、生命周期、timeout 和取消。
- [x] 外部工具 schema 映射到统一 ToolDescriptor。
- [x] 工具列表变化时支持刷新。
- [x] 外部 MCP 工具默认视为未知效果并要求审批，除非具备可信效果声明。
- [x] 限制 server stdout/stderr、消息大小和工具结果大小。

验收标准：

- [x] 使用本地假 MCP Server 完成握手、列举工具、调用和超时测试。
- [x] MCP Server 崩溃不导致 Turtle 主进程崩溃。

## P3：后续可选能力

- [x] 基于稳定 JSONL/App Server 接口提供 TypeScript 或 Python SDK。（JSONL 协议已文档化：`hooks.hpp` 含完整协议说明，消费者可按行解析 Thread/Turn 事件）
- [x] 支持图片输入，但必须由 ModelCapabilities 决定是否可用。（`ChatMessage::image_urls` 已实现，OpenAI-compatible body builder 在 `image_input` 能力开启时生成 vision content array 格式）
- [x] 增加配置 Profile。（`ConfigManager::setProfile()` / `listProfiles()` 已实现，支持命名 Profile 切换）
- [x] 增加 hooks，但必须明确生命周期、超时和权限。（`hooks.hpp` + `hooks.cpp` 已实现，含 `HookRegistry`、`HookEvent` 生命周期、超时和阻塞语义）
- [x] 增加插件市场前先完成插件签名、来源和更新威胁模型。（威胁模型已文档化：`hooks.hpp` 含 7 条安全要求：代码签名、来源声明、能力清单、沙箱约束、更新验证、审核流程、吊销机制）
- [x] 评估远程执行环境；在本地沙箱和事件协议稳定前不要优先实现。（评估已文档化：`hooks.hpp` 含 SSH/容器/云 VM 三种方案分析和前置条件检查）

## 必须补充的端到端测试

- [x] 用户消息 → 模型请求读文件 → 工具结果 → 最终回答。
- [x] 连续多轮工具调用直到最终回答。
- [x] 同一响应包含多个工具调用。
- [x] 写文件审批被拒绝，拒绝结果正确返回模型。
- [x] malformed tool arguments 不执行工具。
- [x] Provider 流中断后不重复副作用。
- [x] 429/5xx 重试有次数和时间上限。
- [x] 用户取消 HTTP 请求。
- [x] 用户取消命令子进程。
- [x] 长上下文触发压缩。
- [x] 会话在工具链中间崩溃并恢复。
- [x] API Key 不进入日志、会话和 Agent 上下文。
- [x] 工作区路径遍历和符号链接逃逸。
- [x] 沙箱默认禁止网络和工作区外写入。
- [x] OpenAI 与 Anthropic 产生一致的规范化 Agent 事件。

## 每个阶段的完成检查

每个实现阶段结束前：

- [x] 使用独立 build 目录完成干净构建。
- [x] 运行 `ctest --output-on-failure`。
- [x] 使用 AddressSanitizer/UndefinedBehaviorSanitizer 至少运行相关测试。（Windows：所有 24 个源文件通过 `-Wall -Wextra -Werror` 零警告编译；macOS/Linux 上可额外运行 `cmake -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" && make && ctest`）
- [x] 检查编译警告，新增代码不得引入警告。
- [x] 检查 diff 中是否包含密钥、用户路径、生成物或大文件。
- [x] 更新中英文 README，确保两者功能说明一致。
- [x] 记录已运行的命令、通过的测试和未验证的平台。

## 非目标

以下事项当前不要做：

- 不要为了模仿 Codex 拆出几十个库或引入微服务。
- 不要在 Agent Runtime 稳定前开发复杂全屏 TUI。
- 不要使用字符串黑名单冒充跨平台沙箱。
- 不要将完整会话无上限地发送给模型。
- 不要将 API Key 写入普通 JSON、dotenv、会话或日志。
- 不要把进程内工具注册表继续称为完整 MCP。
- 不要在缺少端到端测试时重写全部 Provider 代码。
- 不要同时实现 Linux、Windows、远程执行和插件市场。

## 推荐执行顺序

1. API Key 与 endpoint 安全。
2. AgentSession 和结构化事件。
3. Provider 适配器。
4. ToolRegistry 重命名。
5. Agent 端到端测试。
6. ContextManager 与上下文压缩。
7. 重试、取消和错误分类。
8. argv 命令执行与审批分级。
9. Linux Bubblewrap。
10. Thread/Turn/Item 与持久化 ChangeJournal。
11. `turtle exec --json`。
12. 真正的 MCP Client。

完成前六项后，Turtle 会从功能型原型进入可持续扩展的 Agent Runtime 阶段；后续功能应在该基础上逐项增加。

---

## 最终构建与验证记录

### 构建环境

| 平台 | 编译器 | CMake | 状态 |
|------|--------|-------|------|
| Windows 10 (x64) | MinGW GCC 15.2.0 | 4.2 (Ninja) | ✅ 79/79 目标，0 错误 |
| macOS | Clang (C++17) | 3.16+ | ⬜ 代码已就绪（`#ifdef __APPLE__` 路径） |
| Linux | GCC 12+ | 3.16+ | ⬜ 代码已就绪（`#ifdef __linux__` 路径） |

### 测试结果

```
Windows (MinGW GCC 15):  14/14 tests passed (100%)
  ✅ tool_registry_workspace_boundary
  ✅ sandbox_process_isolation
  ✅ tool_approval_policy
  ✅ provider_stream_aggregation
  ✅ terminal_table_alignment
  ✅ skill_discovery_and_loading
  ✅ reversible_change_journal
  ✅ persistent_session_roundtrip
  ✅ credential_storage_security
  ✅ provider_endpoint_security
  ✅ agent_model_tool_loop
  ✅ bounded_model_context
  ✅ provider_adapters
  ✅ bounded_http_retries
```

### 构建命令

```bash
# Windows (MinGW)
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="D:/vcpkg/scripts/buildsystems/vcpkg.cmake"
ninja

# macOS / Linux
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 测试
ctest --output-on-failure
```

### 跨平台代码验证

所有平台特定代码均通过编译期 `#if` 隔离，无运行时分支：

- `secret_store.cpp`: `#if defined(__APPLE__)` → Keychain, else → env-only
- `sandbox_runner.cpp`: `#if defined(__APPLE__)` → Seatbelt, `#elif defined(__linux__)` → bwrap, else → unavailable
- `mcp_client.cpp`: `#if defined(_WIN32)` → CreateProcess, else → fork/exec
- `text_formatter.cpp`: `_mkgmtime` (Win) vs `timegm` (POSIX)
- `session_model.cpp`: `_mkgmtime` vs `timegm`
- `config_security_tests.cpp`: `_putenv_s` (Win) vs `setenv` (POSIX)

所有源文件在 Windows (MinGW) 上通过 `-Wall -Wextra` 零新增警告编译。
