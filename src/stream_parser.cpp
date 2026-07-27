#include "stream_parser.hpp"

#include <map>

namespace opencode {

namespace {

ChatResponse initialResponse() {
    ChatResponse response;
    response.input_tokens = 0;
    response.output_tokens = 0;
    response.success = false;
    return response;
}

ChatResponse parseOpenAIEvents(const std::vector<std::string>& events) {
    ChatResponse response = initialResponse();
    std::map<std::size_t, ToolCall> tool_calls;
    std::map<std::size_t, std::string> argument_fragments;

    for (const auto& event : events) {
        nlohmann::json payload;
        try {
            payload = nlohmann::json::parse(event);
        } catch (...) {
            continue;
        }

        if (payload.contains("error")) {
            response.error_message = payload["error"].is_string()
                ? payload["error"].get<std::string>()
                : payload["error"].dump();
            return response;
        }
        if (payload.contains("usage") && payload["usage"].is_object()) {
            response.input_tokens =
                payload["usage"].value("prompt_tokens", response.input_tokens);
            response.output_tokens =
                payload["usage"].value("completion_tokens", response.output_tokens);
        }
        if (!payload.contains("choices") || payload["choices"].empty()) {
            continue;
        }

        const auto& delta = payload["choices"][0].value(
            "delta", nlohmann::json::object()
        );
        if (delta.contains("content") && delta["content"].is_string()) {
            response.content += delta["content"].get<std::string>();
        }
        if (!delta.contains("tool_calls") || !delta["tool_calls"].is_array()) {
            continue;
        }

        for (const auto& tool_delta : delta["tool_calls"]) {
            const std::size_t index = tool_delta.value("index", std::size_t{0});
            auto& call = tool_calls[index];
            if (tool_delta.contains("id") && tool_delta["id"].is_string()) {
                call.id = tool_delta["id"].get<std::string>();
            }
            call.type = tool_delta.value("type", call.type.empty() ? "function" : call.type);
            if (!tool_delta.contains("function")) {
                continue;
            }
            const auto& function = tool_delta["function"];
            if (function.contains("name") && function["name"].is_string()) {
                call.name += function["name"].get<std::string>();
            }
            if (function.contains("arguments") && function["arguments"].is_string()) {
                argument_fragments[index] += function["arguments"].get<std::string>();
            }
        }
    }

    for (auto& [index, call] : tool_calls) {
        try {
            call.arguments = nlohmann::json::parse(argument_fragments[index]);
        } catch (...) {
            call.arguments = nlohmann::json::object();
        }
        response.tool_calls.push_back(std::move(call));
    }
    response.has_tool_calls = !response.tool_calls.empty();
    response.success = !response.content.empty() || response.has_tool_calls;
    return response;
}

ChatResponse parseAnthropicEvents(const std::vector<std::string>& events) {
    ChatResponse response = initialResponse();
    std::map<std::size_t, ToolCall> tool_calls;
    std::map<std::size_t, std::string> input_fragments;

    for (const auto& event : events) {
        nlohmann::json payload;
        try {
            payload = nlohmann::json::parse(event);
        } catch (...) {
            continue;
        }

        const std::string type = payload.value("type", "");
        if (type == "error") {
            response.error_message = payload.value(
                "error", nlohmann::json::object()
            ).dump();
            return response;
        }
        if (type == "message_start" && payload.contains("message")) {
            response.input_tokens = payload["message"].value(
                "usage", nlohmann::json::object()
            ).value("input_tokens", int64_t{0});
        } else if (type == "message_delta") {
            response.output_tokens = payload.value(
                "usage", nlohmann::json::object()
            ).value("output_tokens", response.output_tokens);
        } else if (type == "content_block_start") {
            const std::size_t index = payload.value("index", std::size_t{0});
            const auto block = payload.value(
                "content_block", nlohmann::json::object()
            );
            if (block.value("type", "") == "tool_use") {
                auto& call = tool_calls[index];
                call.id = block.value("id", "");
                call.name = block.value("name", "");
                call.input = block.value("input", nlohmann::json::object());
            }
        } else if (type == "content_block_delta") {
            const std::size_t index = payload.value("index", std::size_t{0});
            const auto delta = payload.value("delta", nlohmann::json::object());
            if (delta.value("type", "") == "text_delta") {
                response.content += delta.value("text", "");
            } else if (delta.value("type", "") == "input_json_delta") {
                input_fragments[index] += delta.value("partial_json", "");
            }
        }
    }

    for (auto& [index, call] : tool_calls) {
        if (!input_fragments[index].empty()) {
            try {
                call.input = nlohmann::json::parse(input_fragments[index]);
            } catch (...) {
                call.input = nlohmann::json::object();
            }
        }
        response.tool_calls.push_back(std::move(call));
    }
    response.has_tool_calls = !response.tool_calls.empty();
    response.success = !response.content.empty() || response.has_tool_calls;
    return response;
}

} // namespace

ChatResponse parseStreamingEvents(
    const std::vector<std::string>& events,
    const std::string& provider_type
) {
    if (provider_type == "Anthropic") {
        return parseAnthropicEvents(events);
    }
    return parseOpenAIEvents(events);
}

} // namespace opencode
