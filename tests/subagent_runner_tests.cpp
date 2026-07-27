#include "subagent_runner.hpp"
#include <iostream>

namespace {
class FakeProvider final : public opencode::ModelProvider {
public:
    std::string name() const override { return "test"; }
    std::string model() const override { return "user-selected-model"; }
    opencode::ModelCapabilities capabilities() const override { return {}; }
    opencode::ChatResponse request(
        const std::vector<opencode::ChatMessage>& messages,
        const std::vector<nlohmann::json>&,
        const std::function<void(const std::string&)>&
    ) override {
        opencode::ChatResponse response{};
        response.success = messages.size() == 2 &&
            messages.front().content.find("Reviewer") != std::string::npos &&
            messages.back().content == "Inspect the patch";
        response.content = "No defects found";
        response.input_tokens = 4;
        response.output_tokens = 3;
        return response;
    }
    void appendToolResults(
        std::vector<opencode::ChatMessage>&,
        const std::vector<opencode::ToolCall>&,
        const std::vector<nlohmann::json>&
    ) const override {}
};
}

int main() {
    FakeProvider provider;
    opencode::SubagentRunner runner(
        provider, {},
        [](const auto&, const auto&) {
            return nlohmann::json({{"success", true}});
        }
    );
    const auto result = runner.run({
        "Inspect the patch", "Reviewer", "Focus on correctness"
    });
    if (!result.value("success", false) ||
        result.value("model", "") != "user-selected-model" ||
        result.value("response", "") != "No defects found" ||
        result.value("input_tokens", 0) != 4) {
        std::cerr << "FAILED: delegated sub-agent did not return its result\n";
        return 1;
    }
    std::cout << "Sub-agent runner checks passed\n";
    return 0;
}
