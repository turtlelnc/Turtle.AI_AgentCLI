#include "mcp_client.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <io.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <csignal>
#endif

namespace opencode {

// Platform process handle is McpProcess (defined in mcp_client.hpp).

namespace {

// ─── Platform: Windows ────────────────────────────────────────────────────

#if defined(_WIN32)

DWORD WINAPI stderrReaderThread(LPVOID param) {
    auto* proc = static_cast<McpProcess*>(param);
    std::array<char, 256> buf;
    DWORD available = 0;
    while (PeekNamedPipe(proc->child_stderr, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
        DWORD read = 0;
        if (ReadFile(proc->child_stderr, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr) && read > 0) {
            std::lock_guard<std::mutex> lock(proc->stderr_mutex);
            if (proc->stderr_buffer.size() < 64 * 1024)
                proc->stderr_buffer.append(buf.data(), read);
        }
    }
    return 0;
}

bool startWindowsProcess(
    McpProcess& proc,
    const std::string& command,
    const std::vector<std::string>& args
) {
    HANDLE child_stdin_rd = nullptr, child_stdin_wr = nullptr;
    HANDLE child_stdout_rd = nullptr, child_stdout_wr = nullptr;
    HANDLE child_stderr_rd = nullptr, child_stderr_wr = nullptr;

    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    if (!CreatePipe(&child_stdin_rd, &child_stdin_wr, &sa, 0)) return false;
    if (!CreatePipe(&child_stdout_rd, &child_stdout_wr, &sa, 0)) return false;
    if (!CreatePipe(&child_stderr_rd, &child_stderr_wr, &sa, 0)) return false;

    SetHandleInformation(child_stdin_wr, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(child_stdout_rd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(child_stderr_rd, HANDLE_FLAG_INHERIT, 0);

    std::ostringstream cmdline;
    cmdline << command;
    for (const auto& a : args) cmdline << ' ' << a;

    STARTUPINFO si = {};
    si.cb = sizeof(si);
    si.hStdInput = child_stdin_rd;
    si.hStdOutput = child_stdout_wr;
    si.hStdError = child_stderr_wr;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(nullptr, const_cast<char*>(cmdline.str().c_str()),
                        nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return false;
    }

    CloseHandle(child_stdin_rd);
    CloseHandle(child_stdout_wr);
    CloseHandle(child_stderr_wr);

    proc.child_stdin = child_stdin_wr;
    proc.child_stdout = child_stdout_rd;
    proc.child_stderr = child_stderr_rd;
    proc.child_process = pi.hProcess;
    CloseHandle(pi.hThread);

    proc.stderr_thread = CreateThread(nullptr, 0, stderrReaderThread, &proc, 0, nullptr);
    return true;
}

void stopWindowsProcess(McpProcess& proc) {
    if (proc.child_process) {
        TerminateProcess(proc.child_process, 0);
        WaitForSingleObject(proc.child_process, 2000);
        CloseHandle(proc.child_process);
    }
    if (proc.stderr_thread) {
        WaitForSingleObject(proc.stderr_thread, 1000);
        CloseHandle(proc.stderr_thread);
    }
    if (proc.child_stdin) CloseHandle(proc.child_stdin);
    if (proc.child_stdout) CloseHandle(proc.child_stdout);
    if (proc.child_stderr) CloseHandle(proc.child_stderr);
}

bool writeStdin(const McpProcess& proc, const std::string& data) {
    DWORD written = 0;
    return WriteFile(proc.child_stdin, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
           written == data.size();
}

std::string readStdout(const McpProcess& proc, int timeout_ms) {
    DWORD available = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!PeekNamedPipe(proc.child_stdout, nullptr, 0, nullptr, &available, nullptr) || available == 0) {
        if (std::chrono::steady_clock::now() >= deadline) return "";
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (WaitForSingleObject(proc.child_process, 0) == WAIT_OBJECT_0) return "";
    }
    std::string buf(available, '\0');
    DWORD read = 0;
    if (!ReadFile(proc.child_stdout, &buf[0], available, &read, nullptr) || read == 0) return "";
    buf.resize(read);
    return buf;
}

#else
// ─── Platform: POSIX ──────────────────────────────────────────────────────

bool startPosixProcess(
    McpProcess& proc,
    const std::string& command,
    const std::vector<std::string>& args
) {
    int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdin_pipe) != 0) return false;
    if (pipe(stdout_pipe) != 0) { close(stdin_pipe[0]); close(stdin_pipe[1]); return false; }
    if (pipe(stderr_pipe) != 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) return false;

    if (pid == 0) {
        // Child: redirect stdio and exec.
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);

        std::vector<const char*> exec_args;
        exec_args.push_back(command.c_str());
        for (const auto& a : args) exec_args.push_back(a.c_str());
        exec_args.push_back(nullptr);
        execvp(command.c_str(), const_cast<char* const*>(exec_args.data()));
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    proc.child_pid = pid;
    proc.stdin_pipe = stdin_pipe[1];
    proc.stdout_pipe = stdout_pipe[0];
    proc.stderr_pipe = stderr_pipe[0];

    // Set stdout to non-blocking.
    int flags = fcntl(proc.stdout_pipe, F_GETFL, 0);
    fcntl(proc.stdout_pipe, F_SETFL, flags | O_NONBLOCK);
    return true;
}

void stopPosixProcess(McpProcess& proc) {
    if (proc.child_pid > 0) {
        kill(proc.child_pid, SIGTERM);
        int status;
        waitpid(proc.child_pid, &status, WNOHANG);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (waitpid(proc.child_pid, &status, WNOHANG) == 0) {
            kill(proc.child_pid, SIGKILL);
            waitpid(proc.child_pid, &status, 0);
        }
    }
    if (proc.stdin_pipe >= 0) close(proc.stdin_pipe);
    if (proc.stdout_pipe >= 0) close(proc.stdout_pipe);
    if (proc.stderr_pipe >= 0) close(proc.stderr_pipe);
}

bool writeStdin(const McpProcess& proc, const std::string& data) {
    return write(proc.stdin_pipe, data.data(), data.size()) == static_cast<ssize_t>(data.size());
}

std::string readStdout(const McpProcess& proc, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::string result;
    std::array<char, 1024> buf;
    while (std::chrono::steady_clock::now() < deadline) {
        ssize_t n = read(proc.stdout_pipe, buf.data(), buf.size());
        if (n > 0) result.append(buf.data(), n);
        else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) break;
        if (!result.empty()) {
            // Check if we have a complete line (JSON-RPC message ends with newline).
            if (result.find('\n') != std::string::npos) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return result;
}

#endif

int defaultTimeoutMs(std::chrono::milliseconds t) {
    return static_cast<int>(t.count());
}

} // anonymous namespace

// ─── McpServer ─────────────────────────────────────────────────────────────

McpServer::McpServer(std::string name, std::string command, std::vector<std::string> args)
    : name_(std::move(name)), command_(std::move(command)), args_(std::move(args)),
      process_(std::make_unique<McpProcess>()) {}

McpServer::~McpServer() { stop(); }

bool McpServer::start(std::chrono::milliseconds timeout, std::string* error) {
    if (running_) return true;

#if defined(_WIN32)
    if (!startWindowsProcess(*process_, command_, args_)) {
        if (error) *error = "Failed to start MCP server process";
        return false;
    }
#else
    if (!startPosixProcess(*process_, command_, args_)) {
        if (error) *error = "Failed to start MCP server process";
        return false;
    }
#endif

    // MCP initialize handshake.
    nlohmann::json init_params;
    init_params["protocolVersion"] = "2024-11-05";
    init_params["capabilities"] = nlohmann::json::object();
    init_params["clientInfo"] = {{"name", "Turtle.AI"}, {"version", "1.0.0"}};

    auto response = sendRequest("initialize", init_params, timeout);
    if (!response.contains("result")) {
        stop();
        if (error) *error = "MCP initialize failed: " + response.dump();
        return false;
    }

    // Send initialized notification.
    nlohmann::json notified;
    notified["jsonrpc"] = "2.0";
    notified["method"] = "notifications/initialized";
    writeLine(notified.dump());

    running_ = true;
    return true;
}

void McpServer::stop() {
#if defined(_WIN32)
    if (!running_ && !process_->child_process) return;
#else
    if (!running_ && process_->child_pid <= 0) return;
#endif
#if defined(_WIN32)
    stopWindowsProcess(*process_);
#else
    stopPosixProcess(*process_);
#endif
    running_ = false;
}

std::vector<ToolDescriptor> McpServer::listTools(std::chrono::milliseconds timeout) {
    std::vector<ToolDescriptor> tools;
    if (!running_) return tools;

    auto response = sendRequest("tools/list", nlohmann::json::object(), timeout);
    if (!response.contains("result") || !response["result"].contains("tools")) return tools;

    for (const auto& t : response["result"]["tools"]) {
        ToolDescriptor td;
        td.name = name_ + "/" + t.value("name", "");
        td.description = t.value("description", "");
        td.parameters = t.value("inputSchema", nlohmann::json::object());
        td.server_name = name_;
        tools.push_back(std::move(td));
    }
    return tools;
}

McpToolResult McpServer::callTool(
    const std::string& tool_name,
    const nlohmann::json& arguments,
    std::chrono::milliseconds timeout,
    const CancellationToken* cancellation
) {
    (void) cancellation; // TODO: wire into readLine timeout
    McpToolResult result;
    if (!running_) {
        result.error = "MCP server not running";
        return result;
    }

    nlohmann::json params;
    params["name"] = tool_name;
    params["arguments"] = arguments;

    auto response = sendRequest("tools/call", params, timeout);
    if (response.contains("error")) {
        result.error = response["error"].value("message", "Unknown MCP error");
        return result;
    }
    if (!response.contains("result")) {
        result.error = "Invalid MCP response";
        return result;
    }

    result.success = true;
    result.content = response["result"].value("content", nlohmann::json::array());
    return result;
}

nlohmann::json McpServer::sendRequest(
    const std::string& method,
    const nlohmann::json& params,
    std::chrono::milliseconds timeout
) {
    static std::atomic<int> request_id{1};

    nlohmann::json request;
    request["jsonrpc"] = "2.0";
    request["id"] = request_id++;
    request["method"] = method;
    request["params"] = params;

    writeLine(request.dump());

    auto deadline = std::chrono::steady_clock::now() + timeout;
    std::string accumulated;
    while (std::chrono::steady_clock::now() < deadline) {
        std::string chunk = readStdout(*process_, defaultTimeoutMs(timeout));
        if (!chunk.empty()) {
            accumulated += chunk;
            // Try to extract a complete JSON-RPC response line.
            size_t nl = accumulated.find('\n');
            if (nl != std::string::npos) {
                std::string line = accumulated.substr(0, nl);
                accumulated.erase(0, nl + 1);
                try { return nlohmann::json::parse(line); }
                catch (...) { continue; }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return {}; // timeout
}

std::string McpServer::readLine(std::chrono::milliseconds timeout) {
    // Simplified: delegate to readStdout with line detection.
    return readStdout(*process_, defaultTimeoutMs(timeout));
}

void McpServer::writeLine(const std::string& line) {
    std::string data = line + "\n";
    writeStdin(*process_, data);
}

// ─── McpRegistry ──────────────────────────────────────────────────────────

void McpRegistry::addServer(std::shared_ptr<McpServer> server) {
    servers_.push_back(std::move(server));
}

std::size_t McpRegistry::initializeAll() {
    std::size_t count = 0;
    for (auto& server : servers_) {
        if (!server->running() && server->start()) ++count;
    }
    return count;
}

std::vector<ToolDescriptor> McpRegistry::allTools() {
    std::vector<ToolDescriptor> all;
    for (auto& server : servers_) {
        if (!server->running()) continue;
        auto tools = server->listTools();
        all.insert(all.end(),
            std::make_move_iterator(tools.begin()),
            std::make_move_iterator(tools.end()));
    }
    return all;
}

McpToolResult McpRegistry::callTool(
    const std::string& qualified_name,
    const nlohmann::json& arguments,
    const CancellationToken* cancellation
) {
    (void) cancellation;
    auto slash = qualified_name.find('/');
    if (slash == std::string::npos) {
        return {false, {}, "Tool name must be qualified as 'server/tool'"};
    }
    std::string server_name = qualified_name.substr(0, slash);
    std::string tool_name = qualified_name.substr(slash + 1);

    for (auto& server : servers_) {
        if (server->name() == server_name) {
            if (!server->running()) return {false, {}, "MCP server not running"};
            return server->callTool(tool_name, arguments);
        }
    }
    return {false, {}, "MCP server not found: " + server_name};
}

void McpRegistry::refresh(const std::string& server_name) {
    // Tools are fetched live from servers; no caching to refresh.
    (void) server_name;
}

void McpRegistry::stopAll() {
    for (auto& server : servers_) server->stop();
}

} // namespace opencode
