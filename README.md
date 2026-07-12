# Turtle.AI AgentCLI

一个使用 C++17 编写的类 Claude Code CLI 的 AI 助手工具，支持多 API 提供商和 LiteLLM 兼容接口。

## 功能特性

- **多 API 提供商支持**: DeepSeek, OpenAI, Anthropic, LlamaCpp/Ollama
- **Token 追踪与费用计算**: 实时显示每次对话的 token 使用和预估费用
- **Git 集成**: 自动检测仓库状态、未提交改动和远程同步情况
- **MCP 工具系统**: 内置文件读取/写入、目录列表、安全命令执行工具
- **UTF-8 装饰界面**: 简洁美观的 TUI 界面
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
    src/mcp_manager.cpp \
    src/http_client.cpp \
    src/ui.cpp \
    src/prompt.cpp \
    -lcurl \
    -o Turtle.AI_AgentCLI
```

**依赖说明：**
- 需要安装 `libcurl-dev` (Ubuntu/Debian) 或 `libcurl-devel` (CentOS/Fedora)
- 需要安装 `nlohmann-json` 库，或通过 `-I` 指定头文件路径

## 使用方法

```bash
./Turtle.AI_AgentCLI
```

### 配置流程

1. 选择 API 提供商 (1-4):
   - 1: [DeepSeek](https://api.deepseek.com)
   - 2: [OpenAI](https://api.openai.com)
   - 3: [Anthropic](https://api.anthropic.com)
   - 4: [LlamaCpp](https://github.com/ggml-org/llama.cpp)/[Ollama](https://ollama.com) (本地服务)

2. 输入 API Key (本地模式可跳过)

3. 选择工作目录

4. 选择是否启用 Git 集成

### 对话命令

- `exit` / `quit`: 结束会话
- `stats`: 查看当前会话的 token 使用和费用统计

## 模型价格表

内置以下模型的价格信息 (USD/百万 tokens) ，仅供参考:

| 模型 | 输入价格 | 输出价格 |
|------|---------|---------|
| deepseek-chat | $0.14 | $0.28 |
| gpt-4o | $2.50 | $10.00 |
| gpt-4o-mini | $0.15 | $0.60 |
| claude-3-5-sonnet | $3.00 | $15.00 |
| llama-3.1-70b | $0.59 | $0.79 |

## 安全特性

- 危险命令拦截 (rm -rf, sudo, chmod 777 等)
- 路径遍历攻击防护
- 文件大小限制 (读取最大 10KB)
- 命令白名单机制

## 项目结构

```
Turtle.AI_AgentCLI/
├── CMakeLists.txt          # CMake 构建配置
├── include/
│   ├── config_manager.hpp  # 配置管理
│   ├── token_tracker.hpp   # Token 追踪和计费
│   ├── git_manager.hpp     # Git 集成
│   ├── mcp_manager.hpp     # MCP 工具管理
│   ├── http_client.hpp     # HTTP 客户端
│   ├── ui.hpp              # 用户界面
│   └── prompt.hpp          # 提示词管理
├── src/
│   ├── main.cpp            # 主程序入口
│   ├── config_manager.cpp
│   ├── token_tracker.cpp
│   ├── git_manager.cpp
│   ├── mcp_manager.cpp
│   ├── http_client.cpp
│   ├── ui.cpp
│   └── prompt.cpp
└── README.md
```

## 注意事项

- 【风险提示】当前方案基于静态分析，已知局限性/潜在风险为：SSL 验证在开发模式下被禁用，生产环境应启用。请在本地验证跑通后再行合入。
- 本地 LlamaCpp 服务需要用户自行部署并指定端口
- Git 集成需要系统安装 git 命令
- 价格数据基于公开文档，实际价格以官方为准

## License

MIT License

## 鸣谢

[Qwen Coder](coder.qwen.ai) & [Codex](https://chatgpt.com/codex/cloud) 提供代码编写支持
