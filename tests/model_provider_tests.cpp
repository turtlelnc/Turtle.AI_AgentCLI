#include "model_provider.hpp"

#include <iostream>
#include <stdexcept>

namespace {
bool expect(bool condition, const std::string& message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main() {
    opencode::HttpClient client;
    auto openai = opencode::createModelProvider(
        "OpenAI", client, "https://api.openai.com/v1/chat/completions",
        "", "gpt-4o-mini"
    );
    auto anthropic = opencode::createModelProvider(
        "Anthropic", client, "https://api.anthropic.com/v1/messages",
        "", "claude-test"
    );
    auto deepseek = opencode::createModelProvider(
        "DeepSeek", client, "https://api.deepseek.com/v1/chat/completions",
        "", "deepseek-test"
    );
    auto compatible = opencode::createModelProvider(
        "OpenAICompatible", client,
        "https://gateway.example/v1/chat/completions", "", "local-test"
    );

    bool passed = expect(
        openai->name() == "OpenAI" && anthropic->name() == "Anthropic" &&
            deepseek->name() == "DeepSeek" &&
            compatible->name() == "OpenAICompatible",
        "factory creates distinct provider adapters"
    );
    passed &= expect(
        anthropic->capabilities().context_window == 200000 &&
            anthropic->capabilities().image_input &&
            !compatible->capabilities().streaming_usage,
        "provider capabilities differ explicitly"
    );

    opencode::ToolCall call;
    call.id = "call-1";
    call.name = "read_file";
    const std::vector<opencode::ToolCall> calls = {call};
    const std::vector<nlohmann::json> results = {
        {{"success", true}, {"content", "contents"}}
    };
    std::vector<opencode::ChatMessage> openai_messages;
    openai->appendToolResults(openai_messages, calls, results);
    std::vector<opencode::ChatMessage> anthropic_messages;
    anthropic->appendToolResults(anthropic_messages, calls, results);
    passed &= expect(
        openai_messages.size() == 1 &&
            openai_messages[0].role == "tool" &&
            openai_messages[0].tool_call_id == "call-1",
        "OpenAI tool result uses tool role"
    );
    passed &= expect(
        anthropic_messages.size() == 1 &&
            anthropic_messages[0].role == "user" &&
            anthropic_messages[0].content_blocks.is_array() &&
            anthropic_messages[0].content_blocks[0].value(
                "tool_use_id", ""
            ) == "call-1",
        "Anthropic tool result uses content blocks"
    );
    bool rejected = false;
    try {
        (void)opencode::createModelProvider(
            "Unknown", client, "https://example.com", "", "test"
        );
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    passed &= expect(rejected, "unknown providers are rejected");
    return passed ? 0 : 1;
}
