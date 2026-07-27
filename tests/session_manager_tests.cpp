#include "session_manager.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>

namespace {
bool expect(bool condition, const std::string& message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main() {
    namespace fs = std::filesystem;
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    const fs::path workspace =
        fs::temp_directory_path() / ("turtle-session-" + unique);
    fs::create_directories(workspace);

    opencode::SessionManager manager;
    bool passed = expect(manager.setWorkspace(workspace), "workspace is accepted");
    const std::string id = manager.create("OpenAI", "test-model");

    opencode::ToolCall call;
    call.id = "call-1";
    call.type = "function";
    call.name = "read_file";
    call.arguments = {{"path", "README.md"}};
    opencode::ChatMessage assistant;
    assistant.role = "assistant";
    assistant.content = "Checking.";
    assistant.tool_calls.push_back(call);
    opencode::ChatMessage tool;
    tool.role = "tool";
    tool.content = "contents";
    tool.tool_call_id = "call-1";
    const std::vector<opencode::ChatMessage> original = {
        {"system", "old prompt", nullptr, "", {}},
        {"user", "Continue this task", nullptr, "", {}},
        assistant,
        tool
    };

    std::string error;
    passed &= expect(
        manager.save(id, "OpenAI", "test-model", original, &error),
        "complete session is saved: " + error
    );
    std::vector<opencode::ChatMessage> loaded;
    opencode::SessionInfo info;
    passed &= expect(manager.load(id, loaded, info, &error), "session loads: " + error);
    passed &= expect(loaded.size() == original.size(), "all messages round-trip");
    passed &= expect(
        loaded[2].tool_calls.size() == 1 &&
            loaded[2].tool_calls[0].arguments.value("path", "") == "README.md" &&
            loaded[3].tool_call_id == "call-1",
        "tool call protocol context round-trips"
    );
    passed &= expect(
        info.model == "test-model" && manager.list().size() == 1,
        "metadata and discovery round-trip"
    );
    {
        std::ifstream input(
            workspace / ".turtle" / "sessions" / (id + ".json")
        );
        const std::string serialized{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };
        passed &= expect(
            serialized.find("api_key") == std::string::npos &&
                serialized.find("OPENAI_API_KEY") == std::string::npos,
            "session schema contains no credential field"
        );
    }

    std::error_code cleanup_error;
    fs::remove_all(workspace, cleanup_error);
    return passed ? 0 : 1;
}
