#include "agent_session.hpp"

#include <iostream>

namespace {
bool expect(bool condition, const std::string& message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

class FakeProvider final : public opencode::ModelProvider {
public:
    std::function<opencode::ChatResponse(
        const std::vector<opencode::ChatMessage>&,
        const std::function<void(const std::string&)>&
    )> handler;
    std::string name() const override { return "OpenAI"; }
    std::string model() const override { return "gpt-4o-mini"; }
    opencode::ModelCapabilities capabilities() const override { return {}; }
    opencode::ChatResponse request(
        const std::vector<opencode::ChatMessage>& messages,
        const std::vector<nlohmann::json>&,
        const std::function<void(const std::string&)>& delta
    ) override {
        return handler(messages, delta);
    }
    void appendToolResults(
        std::vector<opencode::ChatMessage>& messages,
        const std::vector<opencode::ToolCall>& calls,
        const std::vector<nlohmann::json>& results
    ) const override {
        for (std::size_t i = 0; i < calls.size(); ++i) {
            messages.push_back({
                "tool", results[i].dump(), nullptr, calls[i].id, {}
            });
        }
    }
};
}

int main() {
    std::vector<opencode::ChatMessage> messages = {
        {"system", "test system", nullptr, "", {}}
    };
    opencode::TokenTracker tokens;
    tokens.setModel("gpt-4o-mini");
    int model_calls = 0;
    int tool_calls = 0;
    int persistence_calls = 0;
    std::vector<opencode::AgentEventType> events;

    opencode::AgentSessionCallbacks callbacks;
    FakeProvider provider;
    provider.handler = [&](
        const std::vector<opencode::ChatMessage>& history,
        const std::function<void(const std::string&)>& delta
    ) {
        ++model_calls;
        opencode::ChatResponse response{};
        response.success = true;
        response.input_tokens = 10;
        response.output_tokens = 5;
        if (model_calls == 1) {
            opencode::ToolCall call;
            call.id = "call-1";
            call.type = "function";
            call.name = "read_file";
            call.arguments = {{"path", "README.md"}};
            response.tool_calls.push_back(call);
            opencode::ToolCall second;
            second.id = "call-2";
            second.type = "function";
            second.name = "list_directory";
            second.arguments = {{"path", "src"}};
            response.tool_calls.push_back(second);
        } else {
            const bool received_tool_result =
                !history.empty() && history.back().role == "tool" &&
                history.back().tool_call_id == "call-2";
            response.success = received_tool_result;
            response.error_message =
                received_tool_result ? "" : "missing tool result";
            response.content = "Task complete";
            delta("Task ");
            delta("complete");
        }
        return response;
    };
    callbacks.approve_tool = [](const opencode::ToolApproval&) {
        return true;
    };
    callbacks.execute_tool = [&](
        const std::string& name,
        const nlohmann::json& arguments
    ) {
        ++tool_calls;
        const bool valid =
            (name == "read_file" &&
             arguments.value("path", "") == "README.md") ||
            (name == "list_directory" &&
             arguments.value("path", "") == "src");
        return nlohmann::json({
            {"success", valid},
            {"content", "README contents"}
        });
    };
    callbacks.emit = [&](const opencode::AgentEvent& event) {
        events.push_back(event.type);
    };
    callbacks.persist = [&]() { ++persistence_calls; };

    opencode::AgentSession session(
        provider, messages, {}, tokens, callbacks
    );
    const auto result = session.runTurn("Inspect the project");
    bool passed = expect(result.success, "turn completes");
    passed &= expect(
        result.final_response == "Task complete",
        "final response is returned"
    );
    passed &= expect(
        model_calls == 2 && tool_calls == 2,
        "multiple tools execute before the model continues"
    );
    passed &= expect(
        messages.size() == 6 &&
            messages[1].role == "user" &&
            messages[2].role == "assistant" &&
            messages[3].role == "tool" &&
            messages[4].role == "tool" &&
            messages[5].role == "assistant",
        "complete protocol history is retained"
    );
    passed &= expect(
        tokens.getTotalInputTokens() == 20 &&
            tokens.getTotalOutputTokens() == 10,
        "usage is accounted outside the UI"
    );
    passed &= expect(
        persistence_calls >= 3,
        "history persists at stable loop boundaries"
    );
    passed &= expect(
        !events.empty() &&
            events.front() == opencode::AgentEventType::TurnStarted &&
            events.back() == opencode::AgentEventType::TurnCompleted,
        "structured lifecycle events are emitted"
    );

    std::vector<opencode::ChatMessage> legacy_messages = {
        {"system", "legacy system", nullptr, "", {}}
    };
    opencode::TokenTracker legacy_tokens;
    int legacy_model_calls = 0;
    int legacy_tool_calls = 0;
    opencode::AgentSessionCallbacks legacy_callbacks;
    FakeProvider legacy_provider;
    legacy_provider.handler = [&](
        const auto&, const auto&
    ) {
        opencode::ChatResponse response{};
        response.success = true;
        if (++legacy_model_calls == 1) {
            response.content =
                "<tool_calls><tool_call name=\"read_file\">"
                "{\"path\":\"legacy.txt\"}</tool_call></tool_calls>";
        } else {
            response.content = "Legacy complete";
        }
        return response;
    };
    legacy_callbacks.execute_tool = [&](const auto&, const auto&) {
        ++legacy_tool_calls;
        return nlohmann::json({{"success", true}, {"content", "legacy"}});
    };
    opencode::AgentSession legacy_session(
        legacy_provider, legacy_messages, {}, legacy_tokens,
        std::move(legacy_callbacks)
    );
    passed &= expect(
        legacy_session.runTurn("legacy").success &&
            legacy_tool_calls == 1 && legacy_model_calls == 2,
        "legacy XML tool calls remain in the end-to-end loop"
    );

    std::vector<opencode::ChatMessage> malformed_messages = {
        {"system", "malformed system", nullptr, "", {}}
    };
    opencode::TokenTracker malformed_tokens;
    int malformed_tool_calls = 0;
    opencode::AgentSessionCallbacks malformed_callbacks;
    FakeProvider malformed_provider;
    malformed_provider.handler = [](
        const auto&, const auto&
    ) {
        opencode::ChatResponse response{};
        response.success = true;
        response.content =
            "<tool_calls><tool_call name=\"write_file\">not-json"
            "</tool_call></tool_calls>";
        return response;
    };
    malformed_callbacks.execute_tool = [&](const auto&, const auto&) {
        ++malformed_tool_calls;
        return nlohmann::json({{"success", true}});
    };
    opencode::AgentSession malformed_session(
        malformed_provider, malformed_messages, {}, malformed_tokens,
        std::move(malformed_callbacks)
    );
    malformed_session.runTurn("malformed");
    passed &= expect(
        malformed_tool_calls == 0,
        "malformed legacy tool arguments are never executed"
    );

    std::vector<opencode::ChatMessage> cancelled_messages = {
        {"system", "cancel system", nullptr, "", {}}
    };
    opencode::TokenTracker cancelled_tokens;
    opencode::CancellationToken cancellation;
    cancellation.request();
    int cancelled_model_calls = 0;
    FakeProvider cancelled_provider;
    cancelled_provider.handler = [&](const auto&, const auto&) {
        ++cancelled_model_calls;
        return opencode::ChatResponse{};
    };
    opencode::AgentSession cancelled_session(
        cancelled_provider, cancelled_messages, {}, cancelled_tokens, {},
        &cancellation
    );
    const auto cancelled_result = cancelled_session.runTurn("stop now");
    passed &= expect(
        !cancelled_result.success && cancelled_model_calls == 0,
        "pre-cancelled turn never reaches the model"
    );
    return passed ? 0 : 1;
}
