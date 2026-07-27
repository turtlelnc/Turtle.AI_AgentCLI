[README in English](https://github.com/turtlelnc/Turtle.AI_AgentCLI/blob/main/README_EN.md) ｜[中文 README](https://github.com/turtlelnc/Turtle.AI_AgentCLI/blob/main/README.md)

# Turtle.AI AgentCLI

An AI assistant CLI tool similar to Claude Code CLI, written in C++17, with support for multiple API providers and LiteLLM-compatible interfaces.

## Features

- **Multi-API Provider Support**: DeepSeek, OpenAI, Anthropic, LlamaCpp/Ollama
- **Non-interactive Mode**: `turtle exec` / `turtle exec --json` / `turtle resume` / `turtle doctor`
- **Token Tracking & Cost Calculation**: Real-time display of token usage and estimated costs per conversation
- **Streaming Responses**: Aggregates text, tool calls, and usage deltas from OpenAI/DeepSeek and Anthropic
- **Git Integration**: Automatically detects repository status, uncommitted changes, and remote sync status
- **Built-in Tool Registry**: File read/write, directory listing, argv-based command execution, and shell commands; full MCP Client with stdio transport
- **Skills and Session Recovery**: On-demand workflows plus complete workspace-local conversation autosave and resume
- **Structured Sessions**: Thread/Turn/Item JSONL event log with session fork and crash recovery
- **Persistent Change Journal**: Changes linked to Thread/Turn/ToolCall with file-hash external-modification detection and undo
- **Secure Credentials**: Standard provider environment variables and macOS Keychain; API keys are never written to ordinary config or session files
- **Endpoint Protection**: Official credentials are pinned to official hosts, remote APIs require HTTPS, and authenticated redirects are disabled
- **Agent Runtime**: A standalone `AgentSession` runs the model-tool loop and emits structured events to the terminal UI
- **Provider Abstraction**: `ModelProvider` owns model requests, capability declarations, and provider-specific tool-result encoding
- **Long-context Control**: Full sessions remain intact while each model sees a hard-bounded view; system prompt protected from truncation
- **HTTP Reliability**: Rate limits, selected server errors, and transient network failures use bounded backoff retries without replaying streams that already produced visible output
- **Cooperative Cancellation**: Ctrl-C interrupts model streams, retry waits, and complete command process groups; a second press while busy exits
- **Approval Tiering**: Five-level tool effect classification (ReadOnly/WorkspaceWrite/Network/OutOfWorkspace/Destructive) with session-scoped argv authorization
- **Cross-platform Sandbox**: macOS Seatbelt + Linux Bubblewrap; Windows planned separately; refusal when unavailable
- **Tool Call Cards**: Compact status cards show each tool, target, and result
- **Change Previews**: Colored diffs before file changes and argv-style command previews
- **Adaptive Terminal UI**: UTF-8 decoration, streaming output, animated thinking indicator, status colors, and response timing
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

For a direct macOS build, also append
`-framework Security -framework CoreFoundation`.

**Dependencies:**
- Install `libcurl-dev` (Ubuntu/Debian) or `libcurl-devel` (CentOS/Fedora)
- Install the `nlohmann-json` library, or specify the header path via `-I`

## Usage

### Interactive Mode

```bash
./Turtle.AI_AgentCLI
```

### Non-interactive Mode

```bash
# Single execution, output result, then exit
./Turtle.AI_AgentCLI exec "Analyze test failure"

# JSONL output (stdout) for script parsing
./Turtle.AI_AgentCLI exec --json "Fix this issue"

# Resume a saved session
./Turtle.AI_AgentCLI resume <session-id>

# System health check
./Turtle.AI_AgentCLI doctor
```

### Configuration Process

1. Select API provider (1-5):
   - 1: [DeepSeek](https://api.deepseek.com)
   - 2: [OpenAI](https://api.openai.com)
   - 3: [Anthropic](https://api.anthropic.com)
   - 4: [Llama.cpp](https://github.com/ggml-org/llama.cpp) / [Ollama](https://ollama.com) (local service)
   - 5: Custom OpenAI-compatible endpoint

2. Enter API Key; secret input is not echoed (can be skipped for local mode)

   You may instead set `OPENAI_API_KEY`, `ANTHROPIC_API_KEY`, or
   `DEEPSEEK_API_KEY`. On macOS, an entered credential can be stored in Keychain.

3. Select working directory

4. Choose whether to enable Git integration

### Conversation Commands

- `/help`: Show available commands
- `/stats`: View token usage and cost statistics for the current session
- `/clear`: Clear the terminal
- `/theme`: Show current appearance and available themes
- `/theme ocean|violet|amber|green|mono`: Change the color theme
- `/animation on|off`: Toggle the thinking animation
- `/mouse on|off`: Toggle clickable file links in tool cards
- `/skills`: List available agent skills
- `/session`: Show the current autosaved session ID
- `/sessions`: List saved sessions in the workspace
- `/resume <id>`: Resume a saved session
- `/changes`: List reversible changes
- `/undo [change-id]`: Undo the latest or selected safe change
- `/exit`: End the session

The legacy `help`, `stats`, `clear`, `exit`, and `quit` commands remain supported.

## Model Pricing Table

Built-in pricing information for the following models (USD/million tokens), for reference only:

| Model | Input Price | Output Price |
|-------|-------------|--------------|
| deepseek-v4-flash | API-provider dependent | API-provider dependent |
| deepseek-v4-pro | API-provider dependent | API-provider dependent |
| gpt-4o | $2.50 | $10.00 |
| gpt-4o-mini | $0.15 | $0.60 |
| claude-3-5-sonnet | $3.00 | $15.00 |
| llama-3.1-70b | $0.59 | $0.79 |

## Security Features

- Credentials are read only from standard provider environment variables, macOS Keychain, one-time legacy migration, or current process input
- Non-secret configuration is stored owner-only in `~/.turtle/config.json` and contains no credential field
- Legacy `.opencode_config.json`, `.opencode_env`, and `OPENCODE_API_KEY` remain read-compatible; plaintext files are cleaned after successful Keychain storage, or after confirmation when no secure persistent copy exists
- Official OpenAI, Anthropic, and DeepSeek credentials can only target their built-in HTTPS endpoints
- Custom OpenAI-compatible hosts are a separate provider and require hostname confirmation before credentials are sent
- HTTP is limited to `localhost`, `127.0.0.0/8`, and `[::1]`; every remote endpoint requires HTTPS
- libcurl redirects are disabled so authentication headers cannot cross hosts
- Terminal commands use argv array direct execution (no shell), eliminating command injection; run_shell requires elevated approval
- Five-level tool effect classification for approval: ReadOnly / WorkspaceWrite / Network / OutOfWorkspace / Destructive
- File tools are confined to the selected workspace, including traversal and symlink escape protection
- macOS Seatbelt and Linux Bubblewrap sandboxes: workspace-only writes, no network by default
- File writes, edits, terminal/shell commands, and unknown tools require per-call user approval
- Terminal execution is refused when a native sandbox is unavailable; there is no silent fallback
- File size limits (max read 10KB); large file changes stored as hash
- Supplementary dangerous-argument detection (not the primary security boundary)

## Project Structure

```
Turtle.AI_AgentCLI/
├── CMakeLists.txt          # CMake build configuration
├── include/
│   ├── agent_session.hpp   # Model-tool execution loop and events
│   ├── cancellation.hpp    # Cooperative cancellation token
│   ├── change_journal.hpp  # Persistent reversible changes with file hash
│   ├── config_manager.hpp  # Configuration management
│   ├── context_manager.hpp # Hard context budget and smart compression
│   ├── endpoint_policy.hpp # Provider and endpoint security policy
│   ├── error_category.hpp  # Unified error classification
│   ├── git_manager.hpp     # Git integration
│   ├── http_client.hpp     # Raw HTTP transport (provider-agnostic)
│   ├── http_retry_policy.hpp # Bounded backoff retry policy
│   ├── mcp_client.hpp      # MCP Client (stdio transport)
│   ├── model_provider.hpp  # Model provider interface and capabilities
│   ├── prompt.hpp          # Prompt management
│   ├── sandbox_runner.hpp  # Cross-platform sandbox (Seatbelt / bwrap)
│   ├── secret_store.hpp    # Credential store (env + Keychain)
│   ├── session_manager.hpp # Session autosave and recovery (JSON + JSONL)
│   ├── session_model.hpp   # Thread/Turn/Item structured session model
│   ├── skill_manager.hpp   # Skill discovery and loading
│   ├── stream_parser.hpp   # SSE stream parser
│   ├── text_formatter.hpp  # Terminal table rendering
│   ├── token_tracker.hpp   # Token tracking and billing
│   ├── tool_policy.hpp     # Tool effect classification and approval
│   ├── tool_registry.hpp   # Built-in tool registration and execution
│   └── ui.hpp              # User interface
├── src/                    # Corresponding implementation files
│   ├── main.cpp            # Entry point (interactive + exec/resume/doctor)
│   └── ... (20+ source files)
└── README.md
```

## Notes

- Animations and colors are disabled automatically for non-interactive output; set `NO_COLOR=1` to disable color manually
- Set the startup theme with `TURTLE_THEME`, or disable animation by default with `TURTLE_ANIMATIONS=0`
- Terminals with OSC 8 support can open file paths from tool cards; set `TURTLE_MOUSE=0` to disable this by default
- HTTPS API requests verify certificates and host names by default; local HTTP services are unaffected
- Process sandbox supports macOS (Seatbelt) and Linux (Bubblewrap); on Windows the sandbox is unavailable and terminal tool calls are refused
- Local LlamaCpp services require manual deployment and port specification by the user
- Git integration requires the git command to be installed on the system
- Pricing data is based on public documentation; actual prices are subject to official sources

## License

MIT License

## Acknowledgments

[Qwen Coder](https://coder.qwen.ai) & [Codex](https://chatgpt.com/codex/cloud) for code writing support
