#include "tool_registry.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

} // namespace

int main() {
    namespace fs = std::filesystem;

    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    const fs::path test_root = fs::temp_directory_path() / ("turtle-tools-test-" + unique);
    const fs::path workspace = test_root / "workspace";
    const fs::path outside = test_root / "outside";

    std::error_code error;
    fs::create_directories(workspace, error);
    fs::create_directories(outside, error);
    if (error) {
        std::cerr << "Unable to create test directories: " << error.message() << '\n';
        return 1;
    }

    std::ofstream(outside / "secret.txt") << "secret";

    opencode::ToolRegistry manager;
    bool passed = expect(manager.setWorkspaceRoot(workspace.string()), "workspace root is accepted");

    auto write_result = manager.executeTool(
        "write_file", {{"path", "inside.txt"}, {"content", "inside"}}
    );
    passed &= expect(write_result.value("success", false), "workspace file can be written");

    auto read_result = manager.executeTool("read_file", {{"path", "inside.txt"}});
    passed &= expect(
        read_result.value("content", "") == "inside",
        "workspace file can be read"
    );

    auto outside_result = manager.executeTool(
        "read_file", {{"path", (outside / "secret.txt").string()}}
    );
    passed &= expect(
        !outside_result.value("success", true),
        "absolute path outside workspace is rejected"
    );

    fs::create_directory_symlink(outside, workspace / "escape", error);
    if (!error) {
        auto symlink_result = manager.executeTool(
            "read_file", {{"path", "escape/secret.txt"}}
        );
        passed &= expect(
            !symlink_result.value("success", true),
            "symlink escape is rejected"
        );
    }

    error.clear();
    fs::create_symlink(outside / "secret.txt", workspace / "linked-secret.txt", error);
    if (!error) {
        auto symlink_write_result = manager.executeTool(
            "write_file", {{"path", "linked-secret.txt"}, {"content", "overwritten"}}
        );
        passed &= expect(
            !symlink_write_result.value("success", true),
            "writing through a symlink outside workspace is rejected"
        );
    }

    error.clear();
    const fs::path shell_like_directory = workspace / "quoted\"; touch SHOULD_NOT_EXIST; echo \"";
    fs::create_directory(shell_like_directory, error);
    if (!error) {
        auto list_result = manager.executeTool(
            "list_directory", {{"path", shell_like_directory.filename().string()}}
        );
        passed &= expect(
            list_result.value("success", false),
            "shell-like directory name is listed without command execution"
        );
        passed &= expect(
            !fs::exists(workspace / "SHOULD_NOT_EXIST"),
            "directory listing does not execute shell syntax"
        );
    }

    fs::remove_all(test_root, error);
    return passed ? 0 : 1;
}
