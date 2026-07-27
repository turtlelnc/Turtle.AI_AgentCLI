[README in English](https://github.com/turtlelnc/Turtle.AI_AgentCLI/blob/main/README_EN.md) ｜[中文 README](https://github.com/turtlelnc/Turtle.AI_AgentCLI/blob/main/README.md)

# Turtle.AI AgentCLI

一个使用 C++17 编写的类 Claude Code CLI 的 AI 助手工具，支持多 API 提供商和 LiteLLM 兼容接口。

## 功能特性

- **多 API 提供商支持**: DeepSeek, OpenAI, Anthropic, LlamaCpp/Ollama
- **非交互模式**: `turtle exec` / `turtle exec --json` / `turtle resume` / `turtle doctor`
- **Token 追踪与费用计算**: 实时显示每次对话的 token 使用和预估费用
- **流式响应**: 支持 OpenAI/DeepSeek 与 Anthropic 的文本、工具调用和 usage 增量聚合
- **Git 集成**: 自动检测仓库状态、未提交改动和远程同步情况
- **内置工具注册表**: 提供文件读/写、目录列表、argv 命令执行和 shell 命令；完整 MCP Client 已实现（stdio transport）
- **Skill 工作流**: 按需加载代码审查、问题排查、变更验证、项目理解、安全重构和 Git 工作流
- **结构化会话**: Thread/Turn/Item JSONL 事件日志，支持会话 fork 和崩溃恢复
- **持久变更日志**: 变更关联到 Thread/Turn/ToolCall，支持文件 hash 外部修改检测和撤销
- **安全凭据存储**: 支持 Provider 标准环境变量与 macOS Keychain，API Key 不写入普通配置或会话
- **Endpoint 防护**: 官方凭据绑定官方主机，远程请求强制 HTTPS，带凭据的请求禁止重定向
- **Agent Runtime**: 独立 `AgentSession` 驱动模型—工具闭环，并以结构化事件连接终端 UI
- **Provider 抽象**: `ModelProvider` 封装模型请求、能力声明和 Provider 特有的工具结果编码
- **长上下文控制**: 完整会话原样存档，模型侧按能力硬限额保留系统约束；系统提示词受保护不被截断
- **HTTP 可靠性**: 对限流、部分服务端错误和瞬时网络故障执行有上限的退避重试，并避免重放已经开始输出的流
- **协作式取消**: Ctrl-C 可中断模型流、重试等待和完整命令进程组；繁忙时再次按下才退出程序
- **权限审批分级**: 工具按效果五级分类（只读/工作区写入/网络/工作区外/破坏性），支持会话级精确 argv 授权
- **跨平台沙箱**: macOS Seatbelt + Linux Bubblewrap；Windows 单独规划；沙箱不可用时拒绝执行
- **工具调用卡片**: 以紧凑状态卡展示工具、目标与执行结果
- **变更预览**: 写入和编辑前显示彩色 diff，终端命令以 argv 形式预览
- **自适应终端界面**: UTF-8 装饰、流式输出、动态思考指示器、状态颜色和响应耗时
- **信号处理**: 优雅的中断处理机制

## 编译要求

- GCC 12+ (支持 C++17)
- CMake 3.16+
- libcurl-dev
- nlohmann-json

## 编译方法

先下载 [`main` 的 zip 包 ](https://github.com/turtlelnc/Turtle.AI_AgentCLI/archive/refs/heads/main.zip)并解压

### 方法一：使用 CMake（推荐）

```bash
cd Turtle.AI_AgentCLI
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 方法二：直接使用 G++

如果您没有安装 CMake，可以直接使用 G++ 编译：

```bash
cd Turtle.AI_AgentCLI
g++ -std=c++17 -O2 -I./include \
    src/main.cpp \
    src/config_manager.cpp \
    src/token_tracker.cpp \
    src/git_manager.cpp \
    src/tool_registry.cpp \
    src/sandbox_runner.cpp \
    src/tool_policy.cpp \
    src/stream_parser.cpp \
    src/text_formatter.cpp \
    src/skill_manager.cpp \
    src/change_journal.cpp \
    src/session_manager.cpp \
    src/session_model.cpp \
    src/secret_store.cpp \
    src/endpoint_policy.cpp \
    src/agent_session.cpp \
    src/context_manager.cpp \
    src/model_provider.cpp \
    src/http_client.cpp \
    src/http_retry_policy.cpp \
    src/mcp_client.cpp \
    src/ui.cpp \
    src/prompt.cpp \
    -lcurl \
    -o Turtle.AI_AgentCLI
```

macOS 直接编译时还需追加 `-framework Security -framework CoreFoundation`。

**依赖说明：**
- 需要安装 `libcurl-dev` (Ubuntu/Debian) 或 `libcurl-devel` (CentOS/Fedora)
- 需要安装 `nlohmann-json` 库，或通过 `-I` 指定头文件路径

## 使用方法

### 交互模式

```bash
./Turtle.AI_AgentCLI
```

### 分平台构建

macOS 和 Windows 使用不同的 CMake preset、构建目录和依赖，不能混用缓存：

```bash
# macOS 原生版本：build-macos/Turtle.AI_AgentCLI
cmake --preset macos
cmake --build --preset macos
ctest --preset macos

# Windows x64 交叉编译版本：
# build-windows-x64/Turtle.AI_AgentCLI.exe
cmake --preset windows-x64
cmake --build --preset windows-x64
```

Windows preset 使用
`cmake/toolchains/llvm-mingw-x86_64.cmake`，默认工具链位置为：

```text
/Users/zhangtom/Desktop/Build for Windows/llvm-mingw-20260311-ucrt-macos-universal
```

如工具链移动，可在配置时覆盖：

```bash
cmake --preset windows-x64 \
  -DLLVM_MINGW_ROOT="/absolute/path/to/llvm-mingw"
```

Windows 使用项目内独立的静态 curl：

```text
third_party/curl-windows-x64/
```

需要重建该依赖时，下载并解压 curl 官方源码，然后执行：

```bash
cmake -DCURL_SOURCE_DIR=/absolute/path/to/curl-source \
  -P cmake/build-windows-curl.cmake
```

该构建启用 Windows Schannel，不依赖 OpenSSL，并且不会修改 llvm-mingw
工具链目录。Windows 主程序静态链接 libc++/libunwind，交付时不需要附带
llvm-mingw 运行库 DLL。交叉编译生成的 Windows 测试不能在 macOS 上直接
运行，因此 Windows preset 关闭 `BUILD_TESTING`；平台无关测试由 macOS
preset 执行。

### 非交互模式

```bash
# 单次执行，输出结果后退出
./Turtle.AI_AgentCLI exec "分析测试失败原因"

# JSONL 输出（stdout），便于脚本解析
./Turtle.AI_AgentCLI exec --json "修复这个问题"

# 覆盖配置中的主模型，并启用指定模型的子 Agent
./Turtle.AI_AgentCLI exec --model <main-model> \
  --subagent-model <subagent-model> "审查并修复这个问题"

# 恢复历史会话
./Turtle.AI_AgentCLI resume <session-id>

# 系统健康检查
./Turtle.AI_AgentCLI doctor
```

### 配置流程

1. 选择 API 提供商 (1-5):
   - 1: [DeepSeek](https://api.deepseek.com)
   - 2: [OpenAI](https://api.openai.com)
   - 3: [Anthropic](https://api.anthropic.com)
   - 4: [Llama.cpp(GitHub 仓库)](https://github.com/ggml-org/llama.cpp)/[Ollama](https://ollama.com) (本地服务)
   - 5: 自定义 OpenAI-compatible endpoint

2. 输入 API 支持的准确主模型名称。程序不会推荐或自动填入可能过期的模型名称。

3. 输入 API Key，终端不会回显密钥 (本地模式可跳过)

   也可预先设置 `OPENAI_API_KEY`、`ANTHROPIC_API_KEY` 或
   `DEEPSEEK_API_KEY`。macOS 可选择将输入的密钥保存到系统 Keychain。

4. 选择是否启用子 Agent；启用时输入准确的子 Agent 模型名称，留空则复用主模型。主 Agent 可通过 `delegate_task` 将独立任务交给子 Agent，子 Agent 使用独立上下文且不能递归委派。

5. 选择工作目录

6. 选择是否启用 Git 集成

### 对话命令

- `/help`: 查看可用命令
- `/stats`: 查看当前会话的 token 使用和费用统计
- `/clear`: 清理终端显示
- `/theme`: 查看当前外观和可用主题
- `/theme ocean|violet|amber|green|mono`: 切换颜色主题
- `/animation on|off`: 开关思考动画
- `/mouse on|off`: 开关工具卡片中的可点击文件链接
- `/skills`: 查看当前可用的 Skill
- `/session`: 查看当前自动保存的会话编号
- `/sessions`: 查看当前工作区的历史会话
- `/resume <编号>`: 使用历史会话替换当前对话上下文
- `/goal <目标>`: 启动持续目标模式；macOS 使用 `caffeinate` 保活，Windows 使用系统电源请求 API
- `/goal status|stop`: 查看或停止持续目标
- `/subtask <任务>`: 添加不得覆盖或冲突于主目标的子任务（兼容 `/subtesk`）
- `/diff`: 默认按文件折叠当前 Git diff，仅显示新增/删除行数；可点击折叠按钮或按文件编号展开
- `/mcp load <config.json>`: 从 JSON 加载 stdio MCP 服务并动态刷新工具
- `/memory global|project <内容>`: 将用户偏好或项目事实写入 Markdown 记忆
- `/code-review [范围]`: 使用主 Agent 并在可用时委派子 Agent 做第二轮审查
- `/params`: 查看当前主/子模型和运行参数；`/params goal-max <1-256>` 调整目标连续执行上限

Goal 运行期间会在每轮之间显示连续执行次数、子任务完成比例以及主 Agent/子 Agent 的独立 token 消耗；`/params` 同时显示两者的累计 token 与费用。

交互聊天使用固定底部任务面板：输入框不会被上方输出滚走，下面显示当前主任务/子任务及主、子 Agent 消耗。使用上下方向键选择任务；选择子任务后发送的内容会自动限定在该子任务范围内。Goal 模式中的模型也可通过 `create_subtask` 创建不覆盖主目标的子任务。

在输入框键入 `/` 会打开命令目录；继续输入可按命令名、用法和说明搜索。命令名前缀优先于包含和模糊匹配。使用上下键选择，第一次 Enter 将完整命令填入输入框，补齐参数或确认后再次 Enter 执行。命令目录关闭时，上下键恢复为任务切换。
- `/changes`: 查看本次会话中可撤回的文件和 Skill 修改
- `/undo`: 撤回最近一次修改
- `/undo <编号>`: 撤回指定修改；若存在同目标的更新操作，会要求先撤回较新的操作
- `/exit`: 结束会话

旧的 `help`、`stats`、`clear`、`exit` 和 `quit` 命令仍然兼容。

### 会话保存与恢复

每轮消息和完整工具调用结果都会自动、原子地保存到当前工作区：

```text
.turtle/sessions/session-<timestamp>.json
```

重新启动并选择同一工作区后，CLI 会列出最近会话；输入会话编号即可继续，直接
回车则创建新会话。运行中也可使用 `/sessions` 和 `/resume <编号>`。

恢复内容包括用户与助手消息、Anthropic content blocks、OpenAI/DeepSeek tool
calls、tool call ID 和工具结果。系统提示与 Skill 目录使用当前版本重新生成。
会话文件记录模型和提供商但不保存 API Key；若当前模型与原会话不同，CLI 会提示。
会话目录仅限当前用户访问，单个会话最多 20 MiB、2000 条消息。

### 使用 Skill

Turtle 启动时只向模型提供 Skill 的名称和用途；模型确定需要后，再通过只读工具加载完整说明，避免所有工作流长期占用上下文。可以在自然语言中自动触发，也可以显式指定：

```text
请用 $review-code 检查当前改动
用 $debug-issue 排查这个崩溃
完成后执行 $verify-changes
```

项目内置六个可直接使用的 Skill：`review-code`、`debug-issue`、`verify-changes`、`understand-project`、`refactor-safely` 和 `git-workflow`。

每个 Skill 在加载时都会自动附加统一的闭环执行契约：

1. 明确目标和可观察的完成标准
2. 检查现状并收集证据
3. 在用户授权范围内实际执行
4. 通过测试、构建或直接检查验证结果
5. 验证失败时分析、修正并再次验证
6. 仅在结果已验证、用户拒绝必要操作或存在具体阻塞时停止
7. 汇报修改、验证证据和遗留限制

因此模型不能只输出计划、调用一次工具或修改后未经验证便宣布完成。该契约由运行时注入，也适用于用户后来创建的自定义 Skill。

自定义 Skill 使用包含 YAML frontmatter 的 `SKILL.md`，放在以下任一位置：

- 程序附带的 `skills/<name>/SKILL.md`
- 用户目录 `~/.turtle/skills/<name>/SKILL.md`
- 当前工作区 `.turtle/skills/<name>/SKILL.md`

同名 Skill 按上述顺序覆盖，工作区版本优先级最高。名称只允许小写字母、数字和连字符。

AI 可以在用户明确要求时通过 `manage_skill` 创建、替换或删除工作区
`.turtle/skills` 中的 Skill。每次操作都需要人工确认并写入变更日志；程序内置
Skill 和用户目录 Skill 不会被该工具直接修改。

`write_file`、`edit_file` 和 `manage_skill` 都会返回 `change_id`。撤回记录仅在
当前 CLI 会话内保留，退出程序后不会继续保存。Skill 目录快照最多包含 256 个文件、
总计 2 MiB，超出限制时操作会被拒绝，以确保能够可靠恢复。

## 模型价格表

内置以下模型的价格信息 (USD/百万 tokens) ，仅供参考:

| 模型 | 输入价格 | 输出价格 |
|------|---------|---------|
| deepseek-v4-flash | 价格取决于 API 提供商 | 价格取决于 API 提供商 |
| deepseek-v4-pro | 价格取决于 API 提供商 | 价格取决于 API 提供商 |
| gpt-4o | $2.50 | $10.00 |
| gpt-4o-mini | $0.15 | $0.60 |
| claude-3-5-sonnet | $3.00 | $15.00 |
| llama-3.1-70b | $0.59 | $0.79 |

## 安全特性

- API Key 只从标准环境变量、macOS Keychain、旧配置的一次性迁移或当前进程输入中读取
- 非敏感配置写入 `~/.turtle/config.json`，不包含任何凭据字段，权限为 owner-only
- 旧 `.opencode_config.json` 和 `.opencode_env` 可兼容读取；成功写入 Keychain 后会清除明文，未持久化时会先征得确认，避免误删唯一凭据副本
- OpenAI、Anthropic 和 DeepSeek 官方凭据只能发送到各自内置 HTTPS endpoint
- 自定义 OpenAI-compatible 主机作为独立 Provider；发送凭据前显示目标主机并要求确认
- HTTP 仅允许 `localhost`、`127.0.0.0/8` 和 `[::1]`，所有远程 endpoint 必须使用 HTTPS
- libcurl 不跟随 HTTP 重定向，避免认证 Header 被转发到其他主机
- 终端命令使用 argv 数组直接执行（不经过 shell），消除命令注入面；run_shell 需更高审批
- 工具按效果五级分类审批：只读 / 工作区写入 / 网络 / 工作区外 / 破坏性
- 文件工具限制在所选工作区内，并防止路径遍历和符号链接逃逸
- macOS Seatbelt 和 Linux Bubblewrap 沙箱：仅工作区可写、默认禁止网络
- 写文件、编辑文件、终端命令、shell 命令和未知工具在执行前需要用户逐次确认
- 原生沙箱不可用时拒绝执行终端命令，不静默降级
- 文件大小限制 (读取最大 10KB)；大文件变更以 hash 存储
- 危险参数补充检查（不作为主要安全边界）

## 项目结构

```
Turtle.AI_AgentCLI/
├── CMakeLists.txt          # CMake 构建配置
├── include/
│   ├── config_manager.hpp  # 配置管理
│   ├── token_tracker.hpp   # Token 追踪和计费
│   ├── git_manager.hpp     # Git 集成
│   ├── tool_registry.hpp   # 内置工具注册与执行
│   ├── tool_policy.hpp     # 工具效果分级与审批策略
│   ├── skill_manager.hpp   # Skill 发现与按需加载
│   ├── change_journal.hpp  # 持久可撤回变更日志（含文件 hash）
│   ├── session_manager.hpp # 会话自动保存与恢复（JSON + JSONL）
│   ├── session_model.hpp   # Thread/Turn/Item 结构化会话模型
│   ├── secret_store.hpp    # 环境变量与系统密钥链
│   ├── endpoint_policy.hpp # Provider 与 URL 安全策略
│   ├── agent_session.hpp   # 模型—工具执行闭环与事件
│   ├── model_provider.hpp  # 模型 Provider 接口与能力
│   ├── context_manager.hpp # 上下文硬预算与智能压缩
│   ├── http_client.hpp     # HTTP 原始传输（Provider 无关）
│   ├── http_retry_policy.hpp # 有上限退避重试策略
│   ├── mcp_client.hpp      # MCP Client（stdio transport）
│   ├── sandbox_runner.hpp  # 跨平台沙箱执行（Seatbelt / bwrap）
│   ├── cancellation.hpp    # 协作式取消令牌
│   ├── error_category.hpp  # 统一错误分类
│   ├── stream_parser.hpp   # SSE 流解析器
│   ├── text_formatter.hpp  # 终端表格渲染
│   ├── ui.hpp              # 用户界面
│   └── prompt.hpp          # 提示词管理
├── src/                    # 对应实现文件
│   ├── main.cpp            # 主程序入口（含 exec/resume/doctor）
│   ├── ... (20+ 源文件)
│   └── mcp_client.cpp
├── skills/                 # 内置可复用工作流
└── README.md
```

## 注意事项

- 非交互输出会自动关闭动画和颜色；也可设置 `NO_COLOR=1` 手动禁用颜色效果
- 可用 `TURTLE_THEME` 设置启动主题，用 `TURTLE_ANIMATIONS=0` 默认关闭动画
- 支持 OSC 8 的终端可点击工具卡片中的文件路径；用 `TURTLE_MOUSE=0` 默认关闭
- HTTPS API 请求默认启用证书和主机名校验；本地 HTTP 服务不受影响
- 进程沙箱后端支持 macOS (Seatbelt) 和 Linux (Bubblewrap)；Windows 上沙箱不可用，终端工具调用被拒绝
- 本地 LlamaCpp 服务需要用户自行部署并指定端口
- Git 集成需要系统安装 git 命令
- 价格数据基于公开文档，实际价格以官方为准

## License

MIT License

## 鸣谢

[Qwen Coder](coder.qwen.ai) & [Codex](https://chatgpt.com/codex/cloud) 提供代码编写支持
