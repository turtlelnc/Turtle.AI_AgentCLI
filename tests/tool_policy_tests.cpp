#include "tool_policy.hpp"

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
    bool passed = true;

    // Read-only tools do not require approval.
    passed &= expect(
        !opencode::ToolPolicy::assess("read_file", {{"path", "README.md"}}).required,
        "read_file does not require approval"
    );
    passed &= expect(
        !opencode::ToolPolicy::assess("list_directory", {{"path", "."}}).required,
        "list_directory does not require approval"
    );

    // write_file requires approval with diff preview.
    const auto write = opencode::ToolPolicy::assess(
        "write_file",
        {{"path", "src/main.cpp"}, {"content", "line one\nline two"}}
    );
    passed &= expect(write.required, "write_file requires approval");
    passed &= expect(write.detail == "src/main.cpp", "write approval names its target");
    passed &= expect(
        write.preview.find("+ line one") != std::string::npos,
        "write approval includes a diff preview"
    );
    passed &= expect(
        write.effect.effects.size() == 1 &&
            write.effect.effects[0] == opencode::ToolEffect::WorkspaceWrite,
        "write_file is classified as WorkspaceWrite"
    );

    // run_terminal uses argv array — approval shows argv preview.
    const auto terminal_argv =
        opencode::ToolPolicy::assess("run_terminal", {
            {"argv", nlohmann::json::array({"git", "status"})}
        });
    passed &= expect(terminal_argv.required, "run_terminal requires approval");
    passed &= expect(
        terminal_argv.detail.find("git status") != std::string::npos,
        "terminal approval shows argv elements"
    );
    passed &= expect(
        terminal_argv.preview.find("git") != std::string::npos &&
            terminal_argv.preview.find("status") != std::string::npos,
        "terminal approval preview includes argv content"
    );
    passed &= expect(
        terminal_argv.effect.scoped_approval_allowed,
        "run_terminal allows scoped per-session approval"
    );

    // run_terminal with multi-word argument shows quoting.
    const auto terminal_quoted =
        opencode::ToolPolicy::assess("run_terminal", {
            {"argv", nlohmann::json::array({"git", "commit", "-m", "fix bug"})}
        });
    passed &= expect(
        terminal_quoted.detail.find("\"fix bug\"") != std::string::npos,
        "argv preview quotes arguments containing spaces"
    );

    // run_shell requires elevated approval.
    const auto shell =
        opencode::ToolPolicy::assess("run_shell", {{"command", "git status"}});
    passed &= expect(shell.required, "run_shell requires approval");
    passed &= expect(
        shell.preview == "$ git status",
        "run_shell uses shell command preview"
    );
    passed &= expect(
        !shell.effect.scoped_approval_allowed,
        "run_shell does not allow scoped approval"
    );
    bool shell_has_out_of_workspace = false;
    for (const auto& e : shell.effect.effects) {
        if (e == opencode::ToolEffect::OutOfWorkspace) {
            shell_has_out_of_workspace = true;
        }
    }
    passed &= expect(
        shell_has_out_of_workspace,
        "run_shell classified as OutOfWorkspace (elevated risk)"
    );

    // Unknown tools fail closed.
    const auto unknown =
        opencode::ToolPolicy::assess("custom_tool", nlohmann::json::object());
    passed &= expect(unknown.required, "unknown tools fail closed");
    passed &= expect(
        unknown.effect.effects.size() >= 4,
        "unknown tool effect profile is maximally restrictive"
    );

    // classify() returns correct profiles.
    const auto ro = opencode::ToolPolicy::classify("read_file");
    passed &= expect(
        ro.effects.size() == 1 && ro.effects[0] == opencode::ToolEffect::ReadOnly,
        "classify: read_file is ReadOnly"
    );

    const auto cmd = opencode::ToolPolicy::classify("run_terminal");
    passed &= expect(cmd.scoped_approval_allowed, "classify: run_terminal allows scoping");

    return passed ? 0 : 1;
}
