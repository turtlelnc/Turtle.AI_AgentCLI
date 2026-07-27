#include "session_controls.hpp"
#include <filesystem>
#include <iostream>

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        "turtle-session-controls-test";
    std::error_code error;
    std::filesystem::remove_all(root, error);

    opencode::MemoryStore memory(root);
    if (!memory.appendProject("Project notes", "Prefer focused tests") ||
        memory.promptContext().find("Prefer focused tests") ==
            std::string::npos) {
        std::cerr << "FAILED: project memory round trip\n";
        return 1;
    }

    opencode::GoalState goal;
    if (!goal.start("Ship the feature") ||
        !goal.addSubtask("Add regression tests") ||
        !goal.completeSubtask(1) ||
        goal.prompt().find("[x] Add regression tests") == std::string::npos ||
        goal.prompt().find("architecture -> development -> tests") ==
            std::string::npos) {
        std::cerr << "FAILED: goal and subtask state\n";
        return 1;
    }

    std::filesystem::remove_all(root, error);
    std::cout << "Session control checks passed\n";
    return 0;
}
