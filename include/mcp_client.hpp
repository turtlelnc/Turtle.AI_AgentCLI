#ifndef MCP_CLIENT_HPP
#define MCP_CLIENT_HPP

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "cancellation.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace opencode {

/// Process handle for MCP server subprocess.
struct McpProcess {
#if defined(_WIN32)
    HANDLE child_stdin = nullptr;
    HANDLE child_stdout = nullptr;
    HANDLE child_stderr = nullptr;
    HANDLE child_process = nullptr;
    HANDLE stderr_thread = nullptr;
    std::string stderr_buffer;
    std::mutex stderr_mutex;
#else
    pid_t child_pid = -1;
    int stdin_pipe = -1;
    int stdout_pipe = -1;
    int stderr_pipe = -1;
#endif
};

/// Unified tool descriptor shared between built-in and MCP tools.
struct ToolDescriptor {
    std::string name;
    std::string description;
    nlohmann::json parameters;
    std::string server_name;  // empty for built-in tools
};

/// Result of an MCP tool invocation.
struct McpToolResult {
    bool success = false;
    nlohmann::json content;
    std::string error;
};

/// Manages a single MCP server process over stdio transport.
class McpServer {
public:
    using OnStderr = std::function<void(const std::string&)>;
    using OnCrash = std::function<void(const std::string&)>;

    /// Construct with the server command (argv[0]) and its arguments.
    McpServer(
        std::string name,
        std::string command,
        std::vector<std::string> args = {}
    );
    ~McpServer();

    McpServer(const McpServer&) = delete;
    McpServer& operator=(const McpServer&) = delete;

    /// Start the server process and perform MCP initialize handshake.
    /// Returns false on failure.
    bool start(
        std::chrono::milliseconds timeout = std::chrono::seconds(10),
        std::string* error = nullptr
    );

    /// Stop the server process.
    void stop();

    bool running() const { return running_; }
    const std::string& name() const { return name_; }

    /// List tools exposed by this server.
    std::vector<ToolDescriptor> listTools(
        std::chrono::milliseconds timeout = std::chrono::seconds(5)
    );

    /// Call a tool on this server.
    McpToolResult callTool(
        const std::string& tool_name,
        const nlohmann::json& arguments,
        std::chrono::milliseconds timeout = std::chrono::seconds(30),
        const CancellationToken* cancellation = nullptr
    );

    /// Set callback for server stderr output (for diagnostics).
    void setStderrCallback(OnStderr cb) { on_stderr_ = std::move(cb); }

    /// Set callback for unexpected server exit.
    void setCrashCallback(OnCrash cb) { on_crash_ = std::move(cb); }

private:
    std::string name_;
    std::string command_;
    std::vector<std::string> args_;
    bool running_ = false;

    OnStderr on_stderr_;
    OnCrash on_crash_;

private:
    std::unique_ptr<McpProcess> process_;

    nlohmann::json sendRequest(
        const std::string& method,
        const nlohmann::json& params,
        std::chrono::milliseconds timeout
    );
    std::string readLine(std::chrono::milliseconds timeout);
    void writeLine(const std::string& line);

    static constexpr std::size_t kMaxMessageSize = 1 * 1024 * 1024;  // 1 MiB
    static constexpr std::size_t kMaxStderrBytes = 64 * 1024;        // 64 KiB
};

/// Registry of MCP servers.  Each server's tools are namespaced as
/// "server_name/tool_name".
class McpRegistry {
public:
    /// Add a server.  Start is deferred until `initializeAll` or first use.
    void addServer(std::shared_ptr<McpServer> server);

    /// Initialize all uninitialized servers.
    /// Returns the number that started successfully.
    std::size_t initializeAll();

    /// All tool descriptors from all running servers.
    std::vector<ToolDescriptor> allTools();

    /// Call a namespaced tool ("server/tool").
    McpToolResult callTool(
        const std::string& qualified_name,
        const nlohmann::json& arguments,
        const CancellationToken* cancellation = nullptr
    );

    /// Refresh tools from a specific server or all servers.
    void refresh(const std::string& server_name = "");

    /// Stop all servers.
    void stopAll();

    /// Number of registered servers.
    std::size_t size() const { return servers_.size(); }

private:
    std::vector<std::shared_ptr<McpServer>> servers_;
};

} // namespace opencode

#endif
