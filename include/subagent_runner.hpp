#ifndef SUBAGENT_RUNNER_HPP
#define SUBAGENT_RUNNER_HPP

#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "agent_session.hpp"

namespace opencode {

struct SubagentRequest {
    std::string task;
    std::string role;
    std::string context;
};

class SubagentRunner {
public:
    SubagentRunner(
        ModelProvider& provider,
        std::vector<nlohmann::json> tools,
        std::function<nlohmann::json(
            const std::string&, const nlohmann::json&
        )> execute_tool,
        std::function<bool(const ToolApproval&)> approve_tool = {},
        CancellationToken* cancellation = nullptr
    );

    nlohmann::json run(const SubagentRequest& request);
    int64_t totalInputTokens() const {
        return token_tracker_.getTotalInputTokens();
    }
    int64_t totalOutputTokens() const {
        return token_tracker_.getTotalOutputTokens();
    }
    double totalCostUSD() const {
        return token_tracker_.getTotalCostUSD();
    }

private:
    ModelProvider& provider_;
    std::vector<nlohmann::json> tools_;
    std::function<nlohmann::json(
        const std::string&, const nlohmann::json&
    )> execute_tool_;
    std::function<bool(const ToolApproval&)> approve_tool_;
    CancellationToken* cancellation_;
    TokenTracker token_tracker_;
};

} // namespace opencode

#endif
