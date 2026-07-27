#include "stream_parser.hpp"

#include <iostream>
#include <string>
#include <vector>

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

    const std::vector<std::string> openai_events = {
        R"({"choices":[{"delta":{"content":"Hel"}}]})",
        R"({"choices":[{"delta":{"content":"lo"}}]})",
        R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_1","type":"function","function":{"name":"write_","arguments":"{\"path\":"}}]}}]})",
        R"({"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"name":"file","arguments":"\"a.txt\",\"content\":\"x\"}"}}]}}]})",
        R"({"choices":[],"usage":{"prompt_tokens":12,"completion_tokens":7}})"
    };
    const auto openai =
        opencode::parseStreamingEvents(openai_events, "OpenAI");
    passed &= expect(openai.success, "OpenAI stream succeeds");
    passed &= expect(openai.content == "Hello", "OpenAI text deltas are joined");
    passed &= expect(openai.tool_calls.size() == 1, "OpenAI tool deltas form one call");
    if (openai.tool_calls.size() == 1) {
        passed &= expect(
            openai.tool_calls[0].name == "write_file",
            "OpenAI tool name fragments are joined"
        );
        passed &= expect(
            openai.tool_calls[0].arguments.value("path", "") == "a.txt",
            "OpenAI argument fragments form JSON"
        );
    }
    passed &= expect(
        openai.input_tokens == 12 && openai.output_tokens == 7,
        "OpenAI stream usage is retained"
    );

    const std::vector<std::string> anthropic_events = {
        R"({"type":"message_start","message":{"usage":{"input_tokens":20}}})",
        R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hi "}})",
        R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"there"}})",
        R"({"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"tool_1","name":"read_file","input":{}}})",
        R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"path\":"}})",
        R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"\"README.md\"}"}})",
        R"({"type":"message_delta","usage":{"output_tokens":9}})"
    };
    const auto anthropic =
        opencode::parseStreamingEvents(anthropic_events, "Anthropic");
    passed &= expect(anthropic.success, "Anthropic stream succeeds");
    passed &= expect(anthropic.content == "Hi there", "Anthropic text deltas are joined");
    passed &= expect(
        anthropic.tool_calls.size() == 1 &&
        anthropic.tool_calls[0].input.value("path", "") == "README.md",
        "Anthropic tool input deltas form JSON"
    );
    passed &= expect(
        anthropic.input_tokens == 20 && anthropic.output_tokens == 9,
        "Anthropic stream usage is retained"
    );

    return passed ? 0 : 1;
}
