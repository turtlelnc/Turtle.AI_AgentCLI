#ifndef SANDBOX_RUNNER_HPP
#define SANDBOX_RUNNER_HPP

#include <filesystem>
#include <string>
#include <vector>
#include "cancellation.hpp"

namespace opencode {

struct SandboxResult {
    bool launched = false;
    bool timed_out = false;
    bool cancelled = false;
    int exit_code = -1;
    std::string output;
    std::string error;
};

class SandboxRunner {
public:
    static bool isAvailable();

    /// Execute a program directly with the given argument vector (no shell).
    /// The first element of @p argv is the executable name or path.
    static SandboxResult run(
        const std::vector<std::string>& argv,
        const std::filesystem::path& workspace,
        int timeout_seconds = 30,
        std::size_t max_output_bytes = 102400,
        const CancellationToken* cancellation = nullptr
    );

    /// Execute @p command through a shell (/bin/sh -lc).  This carries higher
    /// risk and should require elevated approval compared to direct argv
    /// execution.
    static SandboxResult runShell(
        const std::string& command,
        const std::filesystem::path& workspace,
        int timeout_seconds = 30,
        std::size_t max_output_bytes = 102400,
        const CancellationToken* cancellation = nullptr
    );
};

} // namespace opencode

#endif // SANDBOX_RUNNER_HPP
