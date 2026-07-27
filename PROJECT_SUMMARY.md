# Turtle.AI AgentCLI — 项目代码架构总结

> 生成日期：2026-07-28 | 由 7 个子 Agent 并行扫描生成
> 项目版本：1.0.0 | 许可证：MIT | 仓库：github.com/turtlelnc/Turtle.AI_AgentCLI

---

## 一、项目概述

Turtle.AI AgentCLI 是一个 **AI 助手命令行工具**，功能类似于 Anthropic 的 Claude Code CLI。使用 **C++17** 编写，支持多种 AI API 提供商，定位为**轻量级、单一二进制、多提供商、支持本地模型**的 AI Agent CLI。

### 核心特性一览

| 类别 | 详情 |
|------|------|
| **API 提供商** | DeepSeek、OpenAI、Anthropic、LlamaCpp/Ollama、自定义 OpenAI 兼容端点 |
| **操作模式** | 交互式 REPL、非交互式 `exec`/`exec --json`/`resume`/`doctor` |
| **流式处理** | SSE 文本聚合、工具调用增量解析（OpenAI + Anthropic 格式） |
| **会话管理** | 基于 Thread/Turn 的 JSONL 事件日志，支持分叉和崩溃恢复 |
| **工具系统** | 6 个内置工具 + 完整 MCP 客户端（stdio 传输） |
| **技能框架** | 6 个内置技能，支持按需加载和自定义技能 |
| **安全** | Keychain 密钥存储、端点固定、HTTPS 强制、五级工具审批、Seatbelt/Bubblewrap 沙箱 |
| **令牌跟踪** | 实时使用量和成本估算，内置主流模型定价表 |
| **Git 集成** | 自动状态检测、未提交更改和远程同步检测 |
| **HTTP 可靠性** | 指数退避 + 抖动重试，支持协作式取消 |
| **UI** | 5 种颜色主题、流式输出、Markdown 渲染、OSC-8 文件链接、动画 spinner |

---

## 二、项目结构

```
归档/
├── CMakeLists.txt              # CMake 构建系统（C++17，依赖 libcurl）
├── README.md / README_EN.md    # 中英文文档
├── content.txt                 # Codex 恢复命令（开发制品）
├── todo.md                     # 任务清单（全部完成）
├── include/                    # 25 个头文件
│   └── nlohmann/json.hpp       # 第三方 JSON 库（header-only）
├── src/                        # 24 个源文件
├── tests/                      # 14 个测试文件（~74 个测试场景）
├── skills/                     # 6 个技能定义（各含 SKILL.md + agents/openai.yaml）
└── build/                      # CMake 构建产物
```

---

## 三、模块架构

### 3.1 核心层 — 会话与模型

| 模块 | 头文件 | 源文件 | 行数 | 职责 |
|------|--------|--------|------|------|
| **AgentSession** | `agent_session.hpp` | `agent_session.cpp` | ~309 | 核心对话循环：用户输入 → 模型推理 → 工具执行 → 结果返回，最多 64 步 |
| **SessionManager** | `session_manager.hpp` | `session_manager.cpp` | ~322 | 会话持久化：JSON 文件读写，原子写入（tmp→rename），2000 条消息/20MiB 上限 |
| **SessionModel** | `session_model.hpp` | `session_model.cpp` | ~333 | Thread/Turn/EventLogEntry 数据模型 + JSONL 序列化 + 旧格式迁移 |
| **ModelProvider** | `model_provider.hpp` | `model_provider.cpp` | ~804 | 多提供商抽象层：OpenAIProvider、AnthropicProvider、DeepSeekProvider、OpenAICompatibleProvider |
| **ContextManager** | `context_manager.hpp` | `context_manager.cpp` | ~273 | 上下文窗口管理：按模型能力裁剪消息，确定性滚动摘要，硬上限裁剪 |
| **PromptManager** | `prompt.hpp` | `prompt.cpp` | ~143 | 系统提示构建：Role-Context-Task-Constraint 四段式模板 |
| **TokenTracker** | `token_tracker.hpp` | `token_tracker.cpp` | ~110 | 令牌使用量追踪 + 费用估算（内置 10+ 模型定价表） |

### 3.2 工具与安全层

| 模块 | 头文件 | 源文件 | 行数 | 职责 |
|------|--------|--------|------|------|
| **ToolRegistry** | `tool_registry.hpp` | `tool_registry.cpp` | ~479 | 6 个内置工具：read_file、write_file、edit_file、list_directory、run_terminal、run_shell |
| **ToolPolicy** | `tool_policy.hpp` | `tool_policy.cpp` | ~199 | 五级效果分类（只读/工作区写入/网络/工作区外/破坏性）+ 审批决策 |
| **SandboxRunner** | `sandbox_runner.hpp` | `sandbox_runner.cpp` | ~441 | macOS Seatbelt / Linux Bubblewrap 沙箱，限制文件系统和网络访问 |
| **EndpointPolicy** | `endpoint_policy.hpp` | `endpoint_policy.cpp` | ~196 | 端点安全：HTTPS 强制、官方端点固定、跨主机重定向禁用 |
| **CancellationToken** | `cancellation.hpp` | — | — | 线程安全取消令牌，支持信号处理集成 |
| **ErrorCategory** | `error_category.hpp` | — | — | 错误分类枚举（认证/速率限制/上下文溢出/传输/提供者/工具执行/取消） |

### 3.3 配置与存储层

| 模块 | 头文件 | 源文件 | 行数 | 职责 |
|------|--------|--------|------|------|
| **ConfigManager** | `config_manager.hpp` | `config_manager.cpp` | ~488 | 配置加载/保存/验证、工作区历史、对话记录、命名配置档案 |
| **SecretStore** | `secret_store.hpp` | `secret_store.cpp` | ~183 | 凭证安全存储：环境变量 > macOS Keychain > 回退（API 密钥永不写入明文配置） |
| **ChangeJournal** | `change_journal.hpp` | `change_journal.cpp` | ~332 | 可撤销的文件更改日志：快照捕获、SHA-256 哈希检测、外部修改检测 |

### 3.4 网络与通信层

| 模块 | 头文件 | 源文件 | 行数 | 职责 |
|------|--------|--------|------|------|
| **HttpClient** | `http_client.hpp` | `http_client.cpp` | ~721 | libcurl 封装：流式/非流式 POST、SSE 帧解析、重试委托 |
| **HttpRetryPolicy** | `http_retry_policy.hpp` | `http_retry_policy.cpp` | ~107 | 重试决策：速率限制/服务器错误指数退避 + 抖动，最多 5 次重试 |
| **StreamParser** | `stream_parser.hpp` | `stream_parser.cpp` | ~167 | SSE 事件解析：OpenAI 格式（delta）和 Anthropic 格式（content_block_delta） |
| **McpClient** | `mcp_client.hpp` | `mcp_client.cpp` | ~441 | MCP 协议客户端：子进程管理、JSON-RPC 2.0、tools/list、tools/call、跨平台 |

### 3.5 UI 与格式化层

| 模块 | 头文件 | 源文件 | 行数 | 职责 |
|------|--------|--------|------|------|
| **UI** | `ui.hpp` | `ui.cpp` | ~931 | 终端 UI：欢迎界面、配置向导、流式输出、Markdown 渲染、主题、斜杠命令 |
| **TextFormatter** | `text_formatter.hpp` | `text_formatter.cpp` | ~668 | UTF-8 显示宽度计算（CJK/Emoji/ZWJ）+ Markdown 表格渲染为 box-drawing 字符 |

### 3.6 扩展与集成层

| 模块 | 头文件 | 源文件 | 行数 | 职责 |
|------|--------|--------|------|------|
| **SkillManager** | `skill_manager.hpp` | `skill_manager.cpp` | ~240 | 技能发现/加载/管理：YAML 前置元数据、工作区覆盖、最多 64 个/每技能 64KiB |
| **Hooks** | `hooks.hpp` | `hooks.cpp` | ~43 | 生命周期钩子系统：13 个事件点、阻塞/通过语义 |
| **GitManager** | `git_manager.hpp` | `git_manager.cpp` | ~143 | Git 集成：状态检测、未提交文件、远程同步状态 |
| **Main** | — | `main.cpp` | ~1107 | 入口：CLI 解析、配置向导、REPL 循环、斜杠命令处理 |

---

## 四、技能系统

项目内置 6 个技能，每个包含 `SKILL.md` 定义文件和 `agents/openai.yaml` Agent 接口：

| 技能 | 缩略命令 | 用途 |
|------|----------|------|
| **debug-issue** | `-issue` | 复现和诊断软件缺陷 |
| **review-code** | `-code` | 代码审查：正确性、回归、安全风险、可维护性 |
| **refactor-safely** | `-safely` | 保持行为不变的重构 |
| **verify-changes** | `-changes` | 构建 + 测试验证，含冒烟测试 |
| **understand-project** | `-project` | 映射不熟悉的仓库结构 |
| **git-workflow** | `-workflow` | 检查仓库状态，安全的 Git 操作 |

---

## 五、测试覆盖

14 个测试文件，约 **74 个测试场景**，使用手写测试框架（`expect()` 宏 + `main()` 返回 0/1）：

| 测试文件 | 场景数 | 覆盖重点 |
|----------|--------|----------|
| agent_session_tests | 4-5 | 核心 Agent 循环、工具调用、XML 解析、取消 |
| change_journal_tests | 3 | 撤销冲突检测、顺序撤销、文件创建撤销 |
| config_security_tests | 5-6 | 凭证迁移、清理、环境变量、旧格式兼容 |
| context_manager_tests | 3 | 上下文窗口：短/长历史、大工具结果限制 |
| endpoint_policy_tests | 8 | HTTPS 强制、loopback 检测、DNS 欺骗防御 |
| http_retry_policy_tests | 7 | 认证错误/速率限制/服务器错误/取消分类 |
| model_provider_tests | 4 | 工厂创建、能力差异、工具结果格式化 |
| sandbox_runner_tests | 4 | 执行/工作区写入/外部拒绝/网络隔离/超时 |
| session_manager_tests | 4 | 会话保存/加载/发现/凭证序列化 |
| skill_manager_tests | 6 | 发现/加载/优先级/管理/撤销 |
| stream_parser_tests | 2 | OpenAI + Anthropic SSE 解析 |
| text_formatter_tests | 5 | CJK/Emoji 宽度、Markdown 表格 |
| tool_policy_tests | 7 | 只读自动批准、写入预览、终端/Shell 分级 |
| tool_registry_tests | 5 | 工作区边界、路径遍历、符号链接逃逸、Shell 注入 |

### 验证状态

- **Windows (MinGW GCC 15.2.0)**：14/14 测试全部通过
- **macOS/Linux**：代码通过 `#ifdef` 条件编译适配，零警告（`-Wall -Wextra -Werror`）

---

## 六、架构设计要点

### 6.1 核心循环（AgentSession）

```
用户输入 → ContextManager.build() → Provider.request() →
  有工具调用？→ ToolPolicy.assess() → 用户审批 →
  ToolRegistry.execute() / McpRegistry.callTool() →
  追加结果到历史 → 循环（最多 64 步）
  无工具调用？→ 返回最终响应
```

### 6.2 回调驱动设计

`AgentSession` 通过 `AgentSessionCallbacks` 与外部通信，包含：
- `approve_tool`：工具审批回调
- `execute_tool`：工具执行回调
- `emit`：事件发射（17 种事件类型）
- `persist`：持久化回调

这使得 `AgentSession` 可在交互式和非交互式模式下复用。

### 6.3 安全分层

```
凭证层：SecretStore（Keychain/环境变量，永不写入明文）
  ↓
传输层：EndpointPolicy（HTTPS 强制，端点固定）
  ↓
审批层：ToolPolicy（五级工具效果分类）
  ↓
执行层：SandboxRunner（Seatbelt/Bubblewrap 系统级隔离）
  ↓
审计层：ChangeJournal（可撤销文件更改 + 外部修改检测）
```

### 6.4 平台适配

所有平台特定代码通过编译时 `#if` 隔离：
- **macOS**：Security Framework（Keychain）、`sandbox-exec`（Seatbelt）、`termios`（密码输入）
- **Linux**：`bwrap`（Bubblewrap 沙箱）
- **Windows**：`CreateProcess`/`TerminateProcess`（MCP 进程管理）、Console API（ANSI 支持）

---

## 七、依赖关系

### 外部依赖
- **libcurl**：HTTP 传输（必需）
- **nlohmann/json**：JSON 解析（header-only，已内嵌）
- **pthread**：线程支持（必需）
- **macOS**：Security、CoreFoundation 框架

### 内部依赖层次

```
main.cpp
  ├── UI ← ConfigManager, TextFormatter
  ├── AgentSession ← ModelProvider, ContextManager, ToolPolicy, TokenTracker, CancellationToken
  │     ├── ToolRegistry ← SandboxRunner, ChangeJournal
  │     │     └── McpRegistry ← McpServer
  │     └── ModelProvider → HttpClient → HttpRetryPolicy, EndpointPolicy
  ├── SessionManager ← SessionModel
  ├── SkillManager ← ChangeJournal
  ├── Hooks
  ├── PromptManager
  ├── GitManager
  └── SecretStore
```

---

## 八、文件统计

| 类别 | 文件数 | 总行数（估算） |
|------|--------|---------------|
| 头文件 | 25 | ~3,500 |
| 源文件 | 24 | ~8,500 |
| 测试文件 | 14 | ~2,500 |
| 技能定义 | 6+6 | ~600 |
| **总计** | **~75** | **~15,100** |

---

## 九、关键发现与备注

1. **已完成项目**：根据 `todo.md`，所有 P0-P3 级任务均已完成，三大平台全部验证通过。
2. **前身项目**：代码中有 "opencode" 命名空间残留，`content.txt` 中包含 Codex 恢复命令，表明项目从 AI 辅助编码工具（Codex）迭代而来。
3. **遗留兼容**：支持旧版 XML 格式工具调用（`<tool_calls>`）和旧 `.opencode_config.json` 配置文件迁移。
4. **无外部测试框架**：自研 `expect()` 宏测试框架，保持零额外依赖。
5. **SHA-256 自实现**：ChangeJournal 中的文件哈希为自行实现，避免引入 OpenSSL 依赖。
6. **MCP 协议版本**：实现 `2024-11-05` 版本 MCP 协议。
