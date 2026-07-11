# OpenCode CLI

A C++17 CLI tool similar to Claude Code, providing AI-powered coding assistance.

## Features

- **Multiple AI Providers**: Support for OpenAI, Anthropic, and local Llama.cpp servers
- **Git Integration**: Automatic Git status detection, stash/pull handling with safety checks
- **MCP Tools**: Built-in tools for file operations (read, write, list, search) and command execution
- **Clean TUI**: UTF-8 decorated terminal interface with ASCII art header
- **C++17 Standard**: Compiled with GCC, using modern C++ features
- **Safety Features**: Command timeout, dangerous command blocking, configuration validation
- **Signal Handling**: Graceful Ctrl+C handling

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
2. Enter API key or Llama.cpp server details (host/port)
3. Choose working directory
4. Enable/disable Git integration
5. Configuration validation
6. Start chatting!

### Commands

- `/q`, `quit`, `exit` - Exit application
- `/h`, `help` - Show help
- `/c`, `clear` - Clear conversation
- `/t`, `tools` - List MCP tools
- `/s`, `status` - Show current configuration

### MCP Tools

Built-in Model Context Protocol tools:

| Tool | Description | Parameters |
|------|-------------|------------|
| `read_file` | Read file contents | `path` |
| `write_file` | Write content to file | `path`, `content` |
| `list_dir` | List directory contents | `path` |
| `search_files` | Search files by pattern | `pattern`, `path` (optional) |
| `run_command` | Execute shell command | `command`, `timeout_ms` (optional, default 30s) |

**Safety Features:**
- Dangerous commands blocked (e.g., `rm -rf /`, `mkfs`, fork bombs)
- Configurable timeout to prevent hanging
- File operations restricted to accessible paths

Users can extend tools by modifying `mcp_manager.cpp`.

## Project Structure

```
opencode-cli/
├── CMakeLists.txt          # Build configuration
├── README.md               # This file
├── include/
│   ├── config_manager.hpp  # Configuration handling + validation
│   ├── git_manager.hpp     # Git operations (status, stash, pull)
│   ├── mcp_manager.hpp     # MCP tool management
│   ├── http_client.hpp     # HTTP requests (libcurl)
│   ├── ui.hpp              # Terminal UI with UTF-8 borders
│   └── prompt.hpp          # Prompt/message management (OpenAI/Anthropic formats)
├── src/
│   ├── main.cpp            # Application entry point + signal handling
│   ├── config_manager.cpp  # Config load/save/validation
│   ├── git_manager.cpp     # Git command execution
│   ├── mcp_manager.cpp     # Built-in tools + user extensible
│   ├── http_client.cpp     # curl wrapper for API calls
│   ├── ui.cpp              # TUI rendering
│   └── prompt.cpp          # Message context management
└── build/                  # Compiled binary
```

## API Provider Details

### OpenAI
- Default model: `gpt-4o`
- Endpoint: `https://api.openai.com/v1/chat/completions`
- Header: `Authorization: Bearer <key>`

### Anthropic
- Default model: `claude-sonnet-4-20250514`
- Endpoint: `https://api.anthropic.com/v1/messages`
- Headers: `x-api-key: <key>`, `x-anthropic-version: 2023-06-01`

### Llama.cpp (Local)
- Default host: `localhost`
- Default port: `8080`
- Endpoint: `http://<host>:<port>/v1/chat/completions`
- No API key required

## Configuration

Config saved to `~/.opencode/config.json`:
```json
{
    "mode": 0,              // 0=OpenAI, 1=Anthropic, 2=LlamaCpp
    "api_key": "...",
    "api_provider": "openai",
    "model": "gpt-4o",
    "llama_host": "localhost",
    "llama_port": 8080,
    "work_dir": "/path/to/project",
    "use_git": true,
    "mcp_configs": []
}
```

## Safety Notes

- **Command Execution**: The `run_command` tool blocks known dangerous patterns
- **Timeout Protection**: Commands timeout after 30s by default
- **File Operations**: Only accessible paths can be read/written
- **Git Safety**: Detects uncommitted changes and remote sync status before modifications

## License

MIT
