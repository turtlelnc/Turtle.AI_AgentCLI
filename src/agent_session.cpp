#include "agent_session.hpp"

#include <cctype>
#include <regex>
#include <utility>

namespace opencode {

namespace {

std::string resultSummary(const nlohmann::json& result) {
    for (const char* key : {"error", "message", "path"}) {
        if (result.contains(key) && result[key].is_string()) {
            return result[key].get<std::string>();
        }
    }
    if (result.contains("exit_code")) {
        return "exit " + std::to_string(result.value("exit_code", -1));
    }
    return "";
}

bool fileMutationTool(const std::string& name) {
    return name == "write_file" || name == "edit_file" ||
           name == "manage_skill";
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string stripFence(std::string value) {
    value = trim(std::move(value));
    if (value.rfind("```", 0) != 0) return value;
    const auto first_line = value.find('\n');
    const auto closing = value.rfind("```");
    if (first_line == std::string::npos || closing <= first_line) return value;
    return trim(value.substr(first_line + 1, closing - first_line - 1));
}

std::vector<ToolCall> parseLegacyToolCalls(const std::string& content) {
    std::vector<ToolCall> calls;
    const std::regex start(
        R"(<(?:｜)?tool_call(?:｜)?\s+[^>]*name\s*=\s*(['"])(.*?)\1[^>]*>)"
    );
    const std::regex end(
        R"((?:</(?:｜)?tool_call(?:｜)?\s*>|<｜/tool_call(?:｜)?\s*>))"
    );
    auto search = content.cbegin();
    std::size_t index = 0;
    while (search != content.cend()) {
        std::smatch opening;
        if (!std::regex_search(search, content.cend(), opening, start)) break;
        const auto body_begin = opening.suffix().first;
        std::smatch closing;
        if (!std::regex_search(body_begin, content.cend(), closing, end)) break;
        try {
            ToolCall call;
            call.id = "legacy-call-" + std::to_string(++index);
            call.type = "function";
            call.name = opening[2].str();
            if (call.name == "terminal" || call.name == "execute_command") {
                call.name = "run_terminal";
            }
            call.arguments = nlohmann::json::parse(stripFence(
                std::string(body_begin, closing.prefix().second)
            ));
            if (call.arguments.is_object()) calls.push_back(std::move(call));
        } catch (const std::exception& e) {
            ToolCall call;
            call.id = "legacy-call-" + std::to_string(++index);
            call.name = opening[2].str();
            if (call.name == "terminal" || call.name == "execute_command") {
                call.name = "run_terminal";
            }
            call.arguments_error =
                std::string("Malformed legacy tool arguments: ") + e.what();
            calls.push_back(std::move(call));
        }
        search = closing.suffix().first;
    }
    return calls;
}

} // namespace

AgentSession::AgentSession(
    ModelProvider& provider,
    std::vector<ChatMessage>& messages,
    std::vector<nlohmann::json> tools,
    TokenTracker& token_tracker,
    AgentSessionCallbacks callbacks,
    CancellationToken* cancellation
) : provider_(provider),
    messages_(messages),
    tools_(std::move(tools)),
    token_tracker_(token_tracker),
    callbacks_(std::move(callbacks)),
    cancellation_(cancellation) {}

void AgentSession::emit(AgentEvent event) const {
    if (callbacks_.emit) callbacks_.emit(event);
}

void AgentSession::persist() const {
    if (callbacks_.persist) callbacks_.persist();
}

void AgentSession::recordUsage(const ChatResponse& response) {
    token_tracker_.recordTokens(response.input_tokens, response.output_tokens);
    const ModelPricing pricing = TokenTracker::getModelPrice(provider_.model());
    const double turn_cost =
        (static_cast<double>(response.input_tokens) / 1000000.0) *
            pricing.input_price_per_1m +
        (static_cast<double>(response.output_tokens) / 1000000.0) *
            pricing.output_price_per_1m;
    emit({
        AgentEventType::UsageUpdated,
        "", "", "", "", true,
        response.input_tokens,
        response.output_tokens,
        turn_cost,
        token_tracker_.getTotalCostUSD()
    });
}

nlohmann::json AgentSession::runTool(const ToolCall& call) {
    if (!call.arguments_error.empty()) {
        emit({
            AgentEventType::TurnFailed,
            "", call.name,
            "Invalid tool arguments: " + call.arguments_error, "",
            false, 0, 0, 0.0, 0.0
        });
        return {
            {"success", false},
            {"error", "Malformed tool arguments: " + call.arguments_error},
            {"malformed", true}
        };
    }
    const nlohmann::json arguments =
        call.arguments.is_null() ? call.input : call.arguments;
    const ToolApproval approval = ToolPolicy::assess(call.name, arguments);
    emit({
        AgentEventType::ToolRequested,
        "", call.name, approval.detail, approval.preview,
        true, 0, 0, 0.0, 0.0
    });
    if (approval.required &&
        (!callbacks_.approve_tool || !callbacks_.approve_tool(approval))) {
        emit({
            AgentEventType::ToolDenied,
            "", call.name, "Denied by user", "", false, 0, 0, 0.0, 0.0
        });
        return {
            {"success", false},
            {"error", "User denied tool execution"},
            {"denied", true}
        };
    }
    emit({
        AgentEventType::ToolApproved,
        "", call.name, approval.detail, "", true, 0, 0, 0.0, 0.0
    });
    emit({
        AgentEventType::ToolStarted,
        "", call.name, approval.detail, "", true, 0, 0, 0.0, 0.0
    });
    nlohmann::json result;
    try {
        result = callbacks_.execute_tool
            ? callbacks_.execute_tool(call.name, arguments)
            : nlohmann::json({
                {"success", false}, {"error", "No tool executor configured"}
            });
    } catch (const std::exception& error) {
        result = {{"success", false}, {"error", error.what()}};
    }
    const bool success = result.value("success", false);
    emit({
        AgentEventType::ToolCompleted,
        "", call.name, resultSummary(result), "", success, 0, 0, 0.0, 0.0
    });
    if (success && fileMutationTool(call.name)) {
        emit({
            AgentEventType::FileChanged,
            "", call.name, result.value("path", ""), "", true,
            0, 0, 0.0, 0.0
        });
    }
    return result;
}

AgentRunResult AgentSession::runTurn(const std::string& user_input) {
    if (user_input.empty()) {
        return {false, "", "User input or model callback is missing"};
    }
    messages_.push_back({"user", user_input, nullptr, "", {}});
    persist();
    emit({
        AgentEventType::TurnStarted,
        user_input, "", "", "", true, 0, 0, 0.0, 0.0
    });

    constexpr std::size_t kMaxModelSteps = 64;
    for (std::size_t step = 0; step < kMaxModelSteps; ++step) {
        if (cancellation_ && cancellation_->requested()) {
            const std::string error = "Operation cancelled";
            emit({AgentEventType::TurnFailed, error, "", "cancelled", "",
                  false, 0, 0, 0.0, 0.0});
            persist();
            return {false, "", error};
        }
        bool streamed = false;
        emit({
            AgentEventType::ModelRequestStarted,
            "", "", "", "", true, 0, 0, 0.0, 0.0
        });
        const ContextWindow context =
            context_manager_.build(messages_, provider_.capabilities());
        if (context.stats.compressed) {
            emit({
                AgentEventType::ContextCompacted,
                "", "",
                "Using " + std::to_string(context.stats.visible_messages) +
                    " visible messages; summarized " +
                    std::to_string(context.stats.omitted_messages) +
                    " older messages",
                "", true, 0, 0, 0.0, 0.0
            });
        }
        ChatResponse response = provider_.request(
            context.messages,
            tools_,
            [&](const std::string& chunk) {
                streamed = true;
                emit({
                    AgentEventType::ModelTextDelta,
                    chunk, "", "", "", true, 0, 0, 0.0, 0.0
                });
            }
        );
        if (!response.success) {
            emit({
                AgentEventType::TurnFailed,
                response.error_message, "", "", "", false,
                0, 0, 0.0, 0.0
            });
            persist();
            return {false, "", response.error_message};
        }
        if (!streamed && !response.content.empty()) {
            emit({
                AgentEventType::ModelResponse,
                response.content, "", "", "", true, 0, 0, 0.0, 0.0
            });
        }
        recordUsage(response);
        if (response.tool_calls.empty()) {
            response.tool_calls = parseLegacyToolCalls(response.content);
        }

        if (response.tool_calls.empty()) {
            messages_.push_back({
                "assistant", response.content, response.content_blocks, "", {}
            });
            persist();
            emit({
                AgentEventType::TurnCompleted,
                response.content, "", "", "", true, 0, 0, 0.0, 0.0
            });
            return {true, response.content, ""};
        }

        ChatMessage assistant;
        assistant.role = "assistant";
        assistant.content = response.content;
        assistant.content_blocks = response.content_blocks;
        assistant.tool_calls = response.tool_calls;
        messages_.push_back(std::move(assistant));

        std::vector<nlohmann::json> results;
        results.reserve(response.tool_calls.size());
        for (const auto& call : response.tool_calls) {
            if (cancellation_ && cancellation_->requested()) {
                const std::string error =
                    "Operation cancelled before tool execution";
                emit({AgentEventType::TurnFailed, error, call.name,
                      "cancelled", "", false, 0, 0, 0.0, 0.0});
                persist();
                return {false, "", error};
            }
            results.push_back(runTool(call));
        }
        provider_.appendToolResults(messages_, response.tool_calls, results);
        persist();
    }
    const std::string error = "Agent exceeded 64 model/tool steps";
    emit({
        AgentEventType::TurnFailed,
        error, "", "", "", false, 0, 0, 0.0, 0.0
    });
    return {false, "", error};
}

} // namespace opencode
