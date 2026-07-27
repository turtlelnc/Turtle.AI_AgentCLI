#include "context_manager.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

opencode::ChatMessage message(std::string role, std::string content) {
    opencode::ChatMessage value;
    value.role = std::move(role);
    value.content = std::move(content);
    return value;
}

} // namespace

int main() {
    bool ok = true;
    opencode::ContextManager manager;

    opencode::ModelCapabilities roomy;
    roomy.context_window = 8192;
    roomy.max_output_tokens = 512;
    std::vector<opencode::ChatMessage> short_history{
        message("system", "Follow the project rules."),
        message("user", "Fix the renderer."),
        message("assistant", "Done.")
    };
    const auto unchanged = manager.build(short_history, roomy);
    ok &= expect(!unchanged.stats.compressed, "short history stays intact");
    ok &= expect(unchanged.messages.size() == short_history.size(),
                 "short history keeps every message");

    opencode::ModelCapabilities tight;
    tight.context_window = 384;
    tight.max_output_tokens = 64;
    std::vector<opencode::ChatMessage> long_history{
        message("system", "Never leave the workspace."),
        message("user", "My goal is a responsive CLI.")
    };
    for (int i = 0; i < 30; ++i) {
        long_history.push_back(message("assistant", std::string(180, 'a' + i % 20)));
        long_history.push_back(message("user", "iteration " + std::to_string(i)));
    }
    long_history.push_back(message("user", "CURRENT GOAL: finish context control"));
    const auto original = long_history;
    const auto compacted = manager.build(long_history, tight);
    ok &= expect(compacted.stats.compressed, "long history is compacted");
    ok &= expect(compacted.stats.estimated_tokens <= 192,
                 "visible context respects the conservative tight budget");
    bool has_system = false;
    bool has_current_goal = false;
    for (const auto& item : compacted.messages) {
        has_system |= item.content.find("Never leave") != std::string::npos;
        has_current_goal |=
            item.content.find("CURRENT GOAL") != std::string::npos;
    }
    ok &= expect(has_system, "system constraint remains visible");
    ok &= expect(has_current_goal, "latest user goal remains visible");
    ok &= expect(long_history.back().content == original.back().content,
                 "full history is not rewritten");

    std::vector<opencode::ChatMessage> tool_history{
        message("system", "Use tools safely."),
        message("user", "Inspect the output.")
    };
    auto assistant = message("assistant", "");
    opencode::ToolCall call;
    call.id = "call-1";
    call.name = "read_file";
    call.arguments = {{"path", "large.txt"}};
    assistant.tool_calls.push_back(call);
    tool_history.push_back(assistant);
    auto tool = message("tool", std::string(100000, 'x'));
    tool.tool_call_id = "call-1";
    tool_history.push_back(tool);
    const auto tool_context = manager.build(tool_history, roomy);
    ok &= expect(tool_history.back().content.size() == 100000,
                 "original tool result remains complete");
    bool has_call = false;
    bool tool_was_bounded = false;
    for (const auto& item : tool_context.messages) {
        has_call |= !item.tool_calls.empty() && item.tool_calls.front().id == "call-1";
        tool_was_bounded |= item.role == "tool" && item.content.size() < 100000;
    }
    ok &= expect(has_call, "active tool call remains visible");
    ok &= expect(tool_was_bounded, "visible tool result is bounded");

    return ok ? 0 : 1;
}
