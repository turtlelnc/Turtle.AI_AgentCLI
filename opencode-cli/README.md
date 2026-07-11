# OpenCode CLI

A C++17 CLI tool similar to Claude Code, providing AI-powered coding assistance.

## Features

- **Multiple AI Providers**: Support for OpenAI, Anthropic, and local Llama.cpp servers
- **Git Integration**: Automatic Git status detection, stash/pull handling
- **MCP Tools**: Built-in tools for file operations (read, list, search)
- **Clean TUI**: UTF-8 decorated terminal interface
- **C++17 Standard**: Compiled with GCC, using modern C++ features

## Requirements

- GCC 7+ (tested with GCC 12)
- CMake 3.16+
- libcurl4-openssl-dev
- nlohmann-json3-dev

## Build

```bash
mkdir build && cd build
cmake ..
make
```

## Usage

```bash
./build/opencode
```

### Setup Flow

1. Select AI provider (OpenAI/Anthropic/Llama.cpp)
2. Enter API key or Llama.cpp server details
3. Choose working directory
4. Enable/disable Git integration
5. Start chatting!

### Commands

- `/q`, `quit`, `exit` - Exit application
- `/h`, `help` - Show help
- `/c`, `clear` - Clear conversation
- `/t`, `tools` - List MCP tools

## Project Structure

```
opencode-cli/
├── CMakeLists.txt          # Build configuration
├── include/
│   ├── config_manager.hpp  # Configuration handling
│   ├── git_manager.hpp     # Git operations
│   ├── mcp_manager.hpp     # MCP tool management
│   ├── http_client.hpp     # HTTP requests (libcurl)
│   ├── ui.hpp              # Terminal UI
│   └── prompt.hpp          # Prompt/message management
├── src/
│   ├── main.cpp            # Application entry point
│   ├── config_manager.cpp
│   ├── git_manager.cpp
│   ├── mcp_manager.cpp
│   ├── http_client.cpp
│   ├── ui.cpp
│   └── prompt.cpp
└── README.md
```

## MCP Tools

Built-in Model Context Protocol tools:

- `read_file` - Read file contents
- `list_dir` - List directory contents  
- `search_files` - Search files by pattern

Users can extend by modifying `mcp_manager.cpp`.

## License

MIT
