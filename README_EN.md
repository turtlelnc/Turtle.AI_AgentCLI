# Turtle.AI AgentCLI

An AI assistant CLI tool similar to Claude Code CLI, written in C++17, with support for multiple API providers and LiteLLM-compatible interfaces.

## Features

- **Multi-API Provider Support**: DeepSeek, OpenAI, Anthropic, LlamaCpp/Ollama
- **Token Tracking & Cost Calculation**: Real-time display of token usage and estimated costs per conversation
- **Git Integration**: Automatically detects repository status, uncommitted changes, and remote sync status
- **MCP Tool System**: Built-in file read/write, directory listing, and secure command execution tools
- **UTF-8 Decorative Interface**: Clean and visually appealing TUI interface
- **Signal Handling**: Graceful interrupt handling mechanism

## Build Requirements

- GCC 12+ (with C++17 support)
- CMake 3.16+
- libcurl-dev
- nlohmann-json

## Build Instructions

First, download and extract the [`main` zip package](https://github.com/turtlelnc/Turtle.AI_AgentCLI/archive/refs/heads/main.zip)

### Method 1: Using CMake (Recommended)

```bash
cd Turtle.AI_AgentCLI
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Method 2: Directly Using G++

If you don't have CMake installed, you can compile directly with G++:

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

**Dependencies:**
- Install `libcurl-dev` (Ubuntu/Debian) or `libcurl-devel` (CentOS/Fedora)
- Install the `nlohmann-json` library, or specify the header path via `-I`

## Usage

```bash
./Turtle.AI_AgentCLI
```

### Configuration Process

1. Select API provider (1-4):
   - 1: [DeepSeek](https://api.deepseek.com)
   - 2: [OpenAI](https://api.openai.com)
   - 3: [Anthropic](https://api.anthropic.com)
   - 4: [Llama.cpp](https://github.com/ggml-org/llama.cpp) / [Ollama](https://ollama.com) (local service)

2. Enter API Key (can be skipped for local mode)

3. Select working directory

4. Choose whether to enable Git integration

### Conversation Commands

- `exit` / `quit`: End the session
- `stats`: View token usage and cost statistics for the current session

## Model Pricing Table

Built-in pricing information for the following models (USD/million tokens), for reference only:

| Model | Input Price | Output Price |
|-------|-------------|--------------|
| deepseek-chat | $0.14 | $0.28 |
| gpt-4o | $2.50 | $10.00 |
| gpt-4o-mini | $0.15 | $0.60 |
| claude-3-5-sonnet | $3.00 | $15.00 |
| llama-3.1-70b | $0.59 | $0.79 |

## Security Features

- Dangerous command interception (rm -rf, sudo, chmod 777, etc.)
- Path traversal attack protection
- File size limits (max read 10KB)
- Command whitelist mechanism

## Project Structure

```
Turtle.AI_AgentCLI/
├── CMakeLists.txt          # CMake build configuration
├── include/
│   ├── config_manager.hpp  # Configuration management
│   ├── token_tracker.hpp   # Token tracking and billing
│   ├── git_manager.hpp     # Git integration
│   ├── mcp_manager.hpp     # MCP tool management
│   ├── http_client.hpp     # HTTP client
│   ├── ui.hpp              # User interface
│   └── prompt.hpp          # Prompt management
├── src/
│   ├── main.cpp            # Main program entry
│   ├── config_manager.cpp
│   ├── token_tracker.cpp
│   ├── git_manager.cpp
│   ├── mcp_manager.cpp
│   ├── http_client.cpp
│   ├── ui.cpp
│   └── prompt.cpp
└── README.md
```

## Notes

- **Risk Warning**: The current approach is based on static analysis. Known limitations/potential risks include: SSL verification is disabled in development mode and should be enabled in production. Please validate locally before merging.
- Local LlamaCpp services require manual deployment and port specification by the user
- Git integration requires the git command to be installed on the system
- Pricing data is based on public documentation; actual prices are subject to official sources

## License

MIT License

## Acknowledgments

[Qwen Coder](https://coder.qwen.ai) & [Codex](https://chatgpt.com/codex/cloud) for code writing support
