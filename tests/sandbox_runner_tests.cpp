#include "sandbox_runner.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

[[maybe_unused]] bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

} // namespace

int main() {
#if !defined(__APPLE__) && !defined(__linux__)
    std::cout << "Native sandbox test skipped: unsupported platform\n";
    return 0;
#else
    namespace fs = std::filesystem;
    if (!opencode::SandboxRunner::isAvailable()) {
        std::cout << "Native sandbox test skipped: sandbox unavailable or already nested\n";
        return 0;
    }

    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    const fs::path test_root = fs::temp_directory_path() / ("turtle-sandbox-test-" + unique);
    const fs::path workspace = test_root / "workspace";
    const fs::path outside = test_root / "outside";
    fs::create_directories(workspace);
    fs::create_directories(outside);

    bool passed = true;

    auto basic = opencode::SandboxRunner::runShell(
        "printf sandbox-ok && /usr/bin/touch inside.txt", workspace
    );
    passed &= expect(basic.exit_code == 0, "command runs inside sandbox");
    passed &= expect(basic.output == "sandbox-ok", "sandbox captures output");
    passed &= expect(fs::exists(workspace / "inside.txt"), "workspace write is allowed");

    const fs::path outside_target = outside / "blocked.txt";
    auto escape = opencode::SandboxRunner::runShell(
        "/usr/bin/touch '" + outside_target.string() + "'", workspace
    );
    passed &= expect(escape.exit_code != 0, "write outside workspace is denied");
    passed &= expect(!fs::exists(outside_target), "outside file was not created");

    auto network = opencode::SandboxRunner::runShell(
        "/usr/bin/curl --connect-timeout 1 http://127.0.0.1:9", workspace, 5
    );
    passed &= expect(network.exit_code != 0, "outbound network is denied");

    auto timeout = opencode::SandboxRunner::runShell("/bin/sleep 5", workspace, 1);
    passed &= expect(timeout.timed_out, "long-running process is terminated");
    passed &= expect(timeout.exit_code == 124, "timeout uses exit code 124");

    std::error_code error;
    fs::remove_all(test_root, error);
    return passed ? 0 : 1;
#endif
}
