#ifndef AGENT_SESSION_HPP
#define AGENT_SESSION_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "http_client.hpp"
#include "token_tracker.hpp"
#include "tool_policy.hpp"
#include "model_provider.hpp"
#include "context_manager.hpp"
#include "cancellation.hpp"

namespace opencode {

enum class AgentEventType {
    TurnStarted,
    ModelRequestStarted,
    ContextCompacted,
    ModelTextDelta,
    ModelResponse,
    ToolRequested,
    ToolApproved,
    ToolDenied,
    ToolStarted,
    ToolCompleted,
    UsageUpdated,
    FileChanged,
    TurnCompleted,
    TurnFailed
};

struct AgentEvent {
    AgentEventType type;
    std::string text;
    std::string tool_name;
    std::string detail;
    std::string preview;
    bool success = true;
    int64_t input_tokens = 0;
    int64_t output_tokens = 0;
    double turn_cost_usd = 0.0;
    double total_cost_usd = 0.0;
};

struct AgentRunResult {
    bool success = false;
    std::string final_response;
    std::string error;
};

struct AgentSessionCallbacks {
    std::function<bool(const ToolApproval&)> approve_tool;
    std::function<nlohmann::json(
        const std::string&,
        const nlohmann::json&
    )> execute_tool;
    std::function<void(const AgentEvent&)> emit;
    std::function<void()> persist;
};

class AgentSession {
public:
    AgentSession(
        ModelProvider& provider,
        std::vector<ChatMessage>& messages,
        std::vector<nlohmann::json> tools,
        TokenTracker& token_tracker,
        AgentSessionCallbacks callbacks,
        CancellationToken* cancellation = nullptr
    );

    AgentRunResult runTurn(const std::string& user_input);
    void setTools(std::vector<nlohmann::json> tools) {
        tools_ = std::move(tools);
    }

private:
    ModelProvider& provider_;
    std::vector<ChatMessage>& messages_;
    std::vector<nlohmann::json> tools_;
    TokenTracker& token_tracker_;
    AgentSessionCallbacks callbacks_;
    ContextManager context_manager_;
    CancellationToken* cancellation_;

    void emit(AgentEvent event) const;
    void persist() const;
    void recordUsage(const ChatResponse& response);
    nlohmann::json runTool(const ToolCall& call);
};

} // namespace opencode

#endif
