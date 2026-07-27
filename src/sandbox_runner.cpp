#include "sandbox_runner.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <thread>

#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace opencode {

namespace {

// ─── POSIX process helpers (macOS + Linux) ─────────────────────────────────

#if defined(__APPLE__) || defined(__linux__)

void appendOutput(int fd, std::string& output, std::size_t max_bytes) {
    std::array<char, 4096> buffer;
    while (true) {
        const ssize_t count = read(fd, buffer.data(), buffer.size());
        if (count > 0) {
            const std::size_t space = max_bytes > output.size()
                ? max_bytes - output.size() : 0;
            output.append(buffer.data(), std::min<std::size_t>(space, count));
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        return;
    }
}

/// Fork + exec a child process, collect output, handle timeout + cancel.
/// @p argv must be null-terminated (last element nullptr).
SandboxResult runChild(
    char* const argv[],
    const std::filesystem::path& cwd,
    int timeout_seconds,
    std::size_t max_output_bytes,
    const CancellationToken* cancellation
) {
    SandboxResult result;

    std::error_code ec;
    const auto canonical_cwd = std::filesystem::canonical(cwd, ec);
    if (ec || !std::filesystem::is_directory(canonical_cwd)) {
        result.error = "Workspace cannot be resolved";
        return result;
    }

    int pipe_fd[2];
    if (pipe(pipe_fd) != 0) {
        result.error = std::string("pipe: ") + std::strerror(errno);
        return result;
    }

    const pid_t child = fork();
    if (child < 0) {
        close(pipe_fd[0]); close(pipe_fd[1]);
        result.error = std::string("fork: ") + std::strerror(errno);
        return result;
    }

    if (child == 0) {
        // Child: set up process group, redirect fds, chdir, exec.
        setpgid(0, 0);
        close(pipe_fd[0]);
        if (dup2(pipe_fd[1], STDOUT_FILENO) < 0 ||
            dup2(pipe_fd[1], STDERR_FILENO) < 0) {
            _exit(126);
        }
        close(pipe_fd[1]);

        if (chdir(canonical_cwd.c_str()) != 0) _exit(126);
        execvp(argv[0], argv);
        _exit(127);
    }

    // Parent: drain output, wait with timeout, handle cancel.
    result.launched = true;
    close(pipe_fd[1]);
    const int flags = fcntl(pipe_fd[0], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(pipe_fd[0], F_SETFL, flags | O_NONBLOCK);
    }

    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(timeout_seconds);
    int status = 0;

    while (true) {
        appendOutput(pipe_fd[0], result.output, max_output_bytes);
        const pid_t wr = waitpid(child, &status, WNOHANG);
        if (wr == child) break;
        if (wr < 0) {
            result.error = std::string("waitpid: ") + std::strerror(errno);
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true;
            kill(-child, SIGKILL);
            waitpid(child, &status, 0);
            break;
        }
        if (cancellation && cancellation->requested()) {
            result.cancelled = true;
            kill(-child, SIGTERM);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (waitpid(child, &status, WNOHANG) == 0)
                kill(-child, SIGKILL);
            waitpid(child, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    appendOutput(pipe_fd[0], result.output, max_output_bytes);
    close(pipe_fd[0]);
    if (result.output.size() >= max_output_bytes)
        result.output += "\n... [output truncated]";

    if (result.cancelled) {
        result.exit_code = 130;
        result.error = "Command cancelled";
    } else if (result.timed_out) {
        result.exit_code = 124;
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }
    return result;
}

#endif // __APPLE__ || __linux__

// ─── macOS Seatbelt backend ────────────────────────────────────────────────

#if defined(__APPLE__)

constexpr const char* kSandboxExec = "/usr/bin/sandbox-exec";

constexpr const char* kSeatbeltPolicy = R"SBPL(
(version 1)
(deny default)

(allow process-exec)
(allow process-fork)
(allow signal (target same-sandbox))
(allow process-info* (target same-sandbox))

; Commands may inspect the host, but may only mutate the selected workspace.
(allow file-read*)
(allow file-write* (subpath (param "WORKSPACE")))
(allow file-write-data (literal "/dev/null"))
(allow file-read* file-write* file-ioctl (literal "/dev/null"))

; Common runtime discovery required by shells, compilers, and developer tools.
(allow sysctl-read)
(allow mach-lookup)
(allow user-preference-read)
(allow ipc-posix-shm-read*)

; No network rules are granted: outbound, inbound, and bind are denied.
)SBPL";

bool macOSSandboxAvailable() {
    const char* existing = std::getenv("CODEX_SANDBOX");
    return access(kSandboxExec, X_OK) == 0
        && (!existing || existing[0] == '\0');
}

SandboxResult runMacOSSandbox(
    const std::vector<const char*>& target_argv,
    const std::filesystem::path& workspace,
    int timeout_seconds,
    std::size_t max_output_bytes,
    const CancellationToken* cancellation
) {
    SandboxResult result;
    std::error_code ec;
    const auto cwd = std::filesystem::canonical(workspace, ec);
    if (ec || !std::filesystem::is_directory(cwd)) {
        result.error = "Workspace cannot be resolved";
        return result;
    }

    const std::string ws_param = "WORKSPACE=" + cwd.string();

    // Build the full argv: sandbox-exec -p <policy> -D WORKSPACE=<path> -- <target>
    std::vector<const char*> full;
    full.push_back(kSandboxExec);
    full.push_back("-p");
    full.push_back(kSeatbeltPolicy);
    full.push_back("-D");
    full.push_back(nullptr);  // placeholder for WORKSPACE=...
    full[4] = ws_param.c_str();
    full.push_back("--");
    for (auto a : target_argv) full.push_back(a);
    full.push_back(nullptr);

    // Convert to mutable char* array for execvp.
    std::vector<char*> exec_args;
    for (auto a : full) exec_args.push_back(const_cast<char*>(a));

    return runChild(exec_args.data(), cwd, timeout_seconds,
                    max_output_bytes, cancellation);
}

#endif // __APPLE__

// ─── Linux Bubblewrap backend ──────────────────────────────────────────────

#if defined(__linux__)

const char* findBwrap() {
    for (const char* candidate : {"/usr/bin/bwrap", "/usr/local/bin/bwrap"}) {
        if (access(candidate, X_OK) == 0) return candidate;
    }
    return nullptr;
}

bool bwrapAvailable() { return findBwrap() != nullptr; }

SandboxResult runBwrap(
    const std::vector<const char*>& target_argv,
    const std::filesystem::path& workspace,
    int timeout_seconds,
    std::size_t max_output_bytes,
    const CancellationToken* cancellation
) {
    SandboxResult result;
    std::error_code ec;
    const auto cwd = std::filesystem::canonical(workspace, ec);
    if (ec || !std::filesystem::is_directory(cwd)) {
        result.error = "Workspace cannot be resolved";
        return result;
    }

    const char* bwrap = findBwrap();
    if (!bwrap) {
        result.error = "bwrap not found";
        return result;
    }

    const std::string ws = cwd.string();

    // Build bwrap arguments.
    // bwrap
    //   --ro-bind /usr /usr
    //   --ro-bind /lib /lib (or --ro-bind /lib64 /lib64)
    //   --ro-bind /bin /bin
    //   --symlink /usr/lib /lib (if needed)
    //   --ro-bind /etc/alternatives /etc/alternatives
    //   --proc /proc
    //   --dev /dev
    //   --bind <workspace> <workspace>
    //   --tmpfs /tmp
    //   --unshare-net
    //   --unshare-ipc
    //   --die-with-parent
    //   -- <target_argv...>
    std::vector<const char*> bwrap_args;
    bwrap_args.push_back(bwrap);

    // Read-only system mounts.
    auto roBind = [&](const char* path) {
        if (access(path, F_OK) == 0) {
            bwrap_args.push_back("--ro-bind");
            bwrap_args.push_back(path);
            bwrap_args.push_back(path);
        }
    };
    roBind("/usr");
    roBind("/lib");
    roBind("/lib64");
    roBind("/bin");
    roBind("/sbin");
    roBind("/etc/alternatives");
    roBind("/etc/ssl");
    roBind("/etc/ca-certificates");

    // Symlink /lib → /usr/lib when /lib doesn't exist but /usr/lib does.
    if (access("/lib", F_OK) != 0 && access("/usr/lib", F_OK) == 0) {
        bwrap_args.push_back("--symlink");
        bwrap_args.push_back("/usr/lib");
        bwrap_args.push_back("/lib");
    }

    // /proc and /dev for basic process and file operations.
    bwrap_args.push_back("--proc");
    bwrap_args.push_back("/proc");
    bwrap_args.push_back("--dev");
    bwrap_args.push_back("/dev");

    // Workspace: read-write.
    bwrap_args.push_back("--bind");
    bwrap_args.push_back(ws.c_str());
    bwrap_args.push_back(ws.c_str());

    // Isolated writable /tmp.
    bwrap_args.push_back("--tmpfs");
    bwrap_args.push_back("/tmp");

    // Network isolation.
    bwrap_args.push_back("--unshare-net");
    bwrap_args.push_back("--unshare-ipc");

    // Clean up when parent dies.
    bwrap_args.push_back("--die-with-parent");

    // Separator.
    bwrap_args.push_back("--");

    // Target program.
    for (auto a : target_argv) bwrap_args.push_back(a);
    bwrap_args.push_back(nullptr);

    std::vector<char*> exec_args;
    for (auto a : bwrap_args) exec_args.push_back(const_cast<char*>(a));

    return runChild(exec_args.data(), cwd, timeout_seconds,
                    max_output_bytes, cancellation);
}

#endif // __linux__

} // anonymous namespace

// ─── Public API ────────────────────────────────────────────────────────────

bool SandboxRunner::isAvailable() {
#if defined(__APPLE__)
    return macOSSandboxAvailable();
#elif defined(__linux__)
    return bwrapAvailable();
#else
    return false;
#endif
}

SandboxResult SandboxRunner::run(
    const std::vector<std::string>& argv,
    const std::filesystem::path& workspace,
    int timeout_seconds,
    std::size_t max_output_bytes,
    const CancellationToken* cancellation
) {
    SandboxResult result;

#if !defined(__APPLE__) && !defined(__linux__)
    (void) argv; (void) workspace;
    (void) timeout_seconds; (void) max_output_bytes;
    (void) cancellation;
    result.error = "A native sandbox backend is not available on this platform";
    return result;
#else
    if (!isAvailable()) {
        result.error =
#if defined(__APPLE__)
            "macOS sandbox-exec is unavailable";
#elif defined(__linux__)
            "bwrap (Bubblewrap) is not installed or not executable";
#endif
        return result;
    }
    if (argv.empty()) {
        result.error = "Argument vector is empty";
        return result;
    }

    // Convert to const char* array.
    std::vector<std::string> storage = argv;
    std::vector<const char*> target;
    for (auto& a : storage) target.push_back(a.c_str());
    target.push_back(nullptr);  // null-terminated for exec

#if defined(__APPLE__)
    return runMacOSSandbox(target, workspace, timeout_seconds,
                           max_output_bytes, cancellation);
#elif defined(__linux__)
    return runBwrap(target, workspace, timeout_seconds,
                    max_output_bytes, cancellation);
#endif
#endif
}

SandboxResult SandboxRunner::runShell(
    const std::string& command,
    const std::filesystem::path& workspace,
    int timeout_seconds,
    std::size_t max_output_bytes,
    const CancellationToken* cancellation
) {
    SandboxResult result;

#if !defined(__APPLE__) && !defined(__linux__)
    (void) command; (void) workspace;
    (void) timeout_seconds; (void) max_output_bytes;
    (void) cancellation;
    result.error = "A native sandbox backend is not available on this platform";
    return result;
#else
    if (!isAvailable()) {
        result.error =
#if defined(__APPLE__)
            "macOS sandbox-exec is unavailable";
#elif defined(__linux__)
            "bwrap (Bubblewrap) is not installed or not executable";
#endif
        return result;
    }
    if (command.empty()) {
        result.error = "Command is empty";
        return result;
    }

    std::vector<const char*> target = {"/bin/sh", "-lc", nullptr};
    target[2] = command.c_str();

#if defined(__APPLE__)
    return runMacOSSandbox(target, workspace, timeout_seconds,
                           max_output_bytes, cancellation);
#elif defined(__linux__)
    return runBwrap(target, workspace, timeout_seconds,
                    max_output_bytes, cancellation);
#endif
#endif
}

} // namespace opencode
