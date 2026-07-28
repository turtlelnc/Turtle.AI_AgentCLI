# Turtle.AI AgentCLI

一个使用 C++17 编写的类 Claude Code CLI 的 AI 助手工具，支持多 API
提供商和 LiteLLM 兼容接口。

## 功能特性

-   多 API 支持：DeepSeek、OpenAI、Anthropic、Llama.cpp/Ollama
-   Agent Runtime：模型、工具、会话状态统一管理
-   工具调用：文件读写、目录查看、命令执行、MCP Client
-   Skill 工作流：代码审查、调试、验证、重构、Git 工作流
-   会话管理：自动保存、恢复、分支会话
-   变更记录：记录文件修改并支持撤销
-   安全凭据：支持环境变量和 macOS Keychain
-   Git 集成：查看仓库状态与变更
-   流式响应：支持工具调用和 token 统计
-   权限控制：工具按风险等级审批
-   跨平台支持：macOS / Linux，Windows 持续完善

## 编译

需要：

-   C++17 编译器（GCC 12+ / Clang / MSVC）
-   CMake 3.16+
-   libcurl
-   nlohmann-json

## 编译



### 使用 CMake 构建

需要：

- CMake 3.16+
- C++17 编译器
  - Windows: Visual Studio 2022/2026 或 LLVM-MinGW
  - macOS: Clang
  - Linux: GCC 12+ 或 Clang
- libcurl
- nlohmann-json

#### Windows (Visual Studio)

```powershell
mkdir build
cd build

cmake ..
cmake --build . --config Release
````

生成文件：

```
build/Release/Turtle.AI_AgentCLI.exe
```

#### Linux

```bash
mkdir build
cd build

cmake ..
cmake --build . -j$(nproc)
```

或者：

```bash
make -j$(nproc)
```

#### macOS

```bash
mkdir build
cd build

cmake ..
cmake --build . -j$(sysctl -n hw.ncpu)
```

## 使用

启动：

``` bash
./Turtle.AI_AgentCLI
```

非交互模式：

``` bash
./Turtle.AI_AgentCLI exec "分析这个问题"
```

JSON 输出：

``` bash
./Turtle.AI_AgentCLI exec --json "修复这个问题"
```

恢复会话：

``` bash
./Turtle.AI_AgentCLI resume <session-id>
```

健康检查：

``` bash
./Turtle.AI_AgentCLI doctor
```

## 内置命令

-   `/help` 查看帮助
-   `/stats` 查看 token 和费用
-   `/clear` 清理终端
-   `/skills` 查看 Skill
-   `/session` 查看当前会话
-   `/resume` 恢复会话
-   `/diff` 查看修改
-   `/undo` 撤销修改
-   `/code-review` 执行代码审查
-   `/exit` 退出

## Skill 系统

Skill 采用按需加载模式：

1.  模型判断是否需要 Skill
2.  加载对应工作流
3.  执行任务
4.  验证结果
5.  汇报修改和验证信息

内置 Skill：

-   review-code
-   debug-issue
-   verify-changes
-   understand-project
-   refactor-safely
-   git-workflow

## 安全设计

-   API Key 不保存到普通配置文件
-   HTTPS 默认启用证书验证
-   工具调用支持权限审批
-   文件操作限制在工作区
-   命令执行避免默认经过 shell
-   沙箱不可用时拒绝执行高风险操作

## 项目结构

    Turtle.AI_AgentCLI/
    ├── include/        # 头文件
    ├── src/            # 源代码
    ├── skills/         # Skill 工作流
    ├── CMakeLists.txt
    └── README.md

## 设计目标

Turtle.AI AgentCLI 的目标是提供一个：

-   可控
-   可验证
-   安全
-   可扩展

的本地 AI Agent 运行环境。

模型负责推理，Runtime 负责权限、工具和验证。

## License

MIT License
