#include "subagent_runner.hpp"

namespace opencode {

SubagentRunner::SubagentRunner(
    ModelProvider& provider,
    std::vector<nlohmann::json> tools,
    std::function<nlohmann::json(
        const std::string&, const nlohmann::json&
    )> execute_tool,
    std::function<bool(const ToolApproval&)> approve_tool,
    CancellationToken* cancellation
) : provider_(provider),
    tools_(std::move(tools)),
    execute_tool_(std::move(execute_tool)),
    approve_tool_(std::move(approve_tool)),
    cancellation_(cancellation) {
    token_tracker_.setModel(provider_.model());
}

nlohmann::json SubagentRunner::run(const SubagentRequest& request) {
    if (request.task.empty()) {
        return {{"success", false}, {"error", "Sub-agent task is required"}};
    }

    std::string system =
        "You are a focused sub-agent. Complete only the delegated task and "
        "return a concise, evidence-based result to the parent agent. Do not "
        "attempt to delegate to another agent.";
    if (!request.role.empty()) {
        system += "\nYour assigned role: " + request.role;
    }
    if (!request.context.empty()) {
        system += "\nParent-provided context:\n" + request.context;
    }

    std::vector<ChatMessage> messages = {
        {"system", system, nullptr, "", {}}
    };
    const int64_t input_before = token_tracker_.getTotalInputTokens();
    const int64_t output_before = token_tracker_.getTotalOutputTokens();
    AgentSessionCallbacks callbacks;
    callbacks.execute_tool = execute_tool_;
    callbacks.approve_tool = approve_tool_;

    AgentSession session(
        provider_, messages, tools_, token_tracker_, std::move(callbacks),
        cancellation_
    );
    const AgentRunResult result = session.runTurn(request.task);
    nlohmann::json output = {
        {"success", result.success},
        {"model", provider_.model()},
        {"response", result.final_response},
        {"input_tokens", token_tracker_.getTotalInputTokens() - input_before},
        {"output_tokens", token_tracker_.getTotalOutputTokens() - output_before},
        {"total_input_tokens", token_tracker_.getTotalInputTokens()},
        {"total_output_tokens", token_tracker_.getTotalOutputTokens()},
        {"total_cost_usd", token_tracker_.getTotalCostUSD()}
    };
    if (!result.error.empty()) output["error"] = result.error;
    return output;
}

} // namespace opencode
