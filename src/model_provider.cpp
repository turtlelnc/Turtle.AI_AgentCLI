#include "model_provider.hpp"

#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace opencode {

// ─── helpers ────────────────────────────────────────────────────────────────

namespace {

std::string resultContent(const nlohmann::json& result) {
    if (result.contains("error") && result["error"].is_string()) {
        return "Error: " + result["error"].get<std::string>();
    }
    if (result.contains("output") && result["output"].is_string()) {
        return result["output"].get<std::string>();
    }
    if (result.contains("content") && result["content"].is_string()) {
        return result["content"].get<std::string>();
    }
    return result.dump();
}

ChatResponse errorResponse(
    const std::string& message,
    ErrorCategory category = ErrorCategory::Provider
) {
    ChatResponse r;
    r.success = false;
    r.error_message = message;
    r.error_category = category;
    return r;
}

// ─── OpenAI-format helpers ─────────────────────────────────────────────────

nlohmann::json openAIBody(
    const std::string& model_name,
    const std::vector<ChatMessage>& messages,
    const std::vector<nlohmann::json>& tools,
    bool stream
) {
    nlohmann::json body;
    body["model"] = model_name;
    body["messages"] = nlohmann::json::array();

    for (const auto& msg : messages) {
        nlohmann::json m{{"role", msg.role}};
        // Vision: when user message has images, use content array format.
        if (msg.role == "user" && !msg.image_urls.empty()) {
            nlohmann::json content_array = nlohmann::json::array();
            if (!msg.content.empty())
                content_array.push_back({{"type", "text"}, {"text", msg.content}});
            for (const auto& url : msg.image_urls) {
                content_array.push_back(
                    {{"type", "image_url"}, {"image_url", {{"url", url}}}}
                );
            }
            m["content"] = std::move(content_array);
        } else {
            m["content"] = msg.content;
        }
        if (msg.role == "tool" && !msg.tool_call_id.empty()) {
            m["tool_call_id"] = msg.tool_call_id;
        }
        if (msg.role == "assistant" && !msg.tool_calls.empty()) {
            nlohmann::json tc_array = nlohmann::json::array();
            for (const auto& tc : msg.tool_calls) {
                tc_array.push_back({
                    {"id", tc.id},
                    {"type", tc.type.empty() ? "function" : tc.type},
                    {"function", {
                        {"name", tc.name},
                        {"arguments", tc.arguments.is_null()
                            ? tc.input.dump()
                            : tc.arguments.dump()}
                    }}
                });
            }
            m["tool_calls"] = std::move(tc_array);
        }
        body["messages"].push_back(std::move(m));
    }

    body["stream"] = stream;
    body["temperature"] = 0.7;
    body["max_tokens"] = 4096;
    if (!tools.empty()) {
        body["tools"] = tools;
    }
    return body;
}

std::string openAITextDelta(const std::string& event) {
    try {
        const auto payload = nlohmann::json::parse(event);
        if (payload.contains("choices") && !payload["choices"].empty()) {
            const auto delta = payload["choices"][0].value(
                "delta", nlohmann::json::object()
            );
            if (delta.contains("content") && delta["content"].is_string()) {
                return delta["content"].get<std::string>();
            }
        }
    } catch (...) {}
    return "";
}

ChatResponse parseOpenAIStream(const std::vector<std::string>& events) {
    ChatResponse response;
    response.input_tokens = 0;
    response.output_tokens = 0;
    response.success = false;

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

        for (const auto& td : delta["tool_calls"]) {
            const std::size_t index = td.value("index", std::size_t{0});
            auto& call = tool_calls[index];
            if (td.contains("id") && td["id"].is_string()) {
                call.id = td["id"].get<std::string>();
            }
            call.type = td.value("type", call.type.empty() ? "function" : call.type);
            if (!td.contains("function")) continue;
            const auto& fn = td["function"];
            if (fn.contains("name") && fn["name"].is_string()) {
                call.name += fn["name"].get<std::string>();
            }
            if (fn.contains("arguments") && fn["arguments"].is_string()) {
                argument_fragments[index] += fn["arguments"].get<std::string>();
            }
        }
    }

    for (auto& [index, call] : tool_calls) {
        try {
            call.arguments = nlohmann::json::parse(argument_fragments[index]);
        } catch (const std::exception& e) {
            call.arguments_error = std::string("Malformed tool arguments: ") + e.what();
        }
        response.tool_calls.push_back(std::move(call));
    }
    response.has_tool_calls = !response.tool_calls.empty();
    response.success = !response.content.empty() || response.has_tool_calls;
    return response;
}

ChatResponse parseOpenAINonStreaming(const std::string& body) {
    ChatResponse response;
    response.success = false;
    try {
        auto json = nlohmann::json::parse(body);
        if (json.contains("error")) {
            response.error_message = json["error"].is_string()
                ? json["error"].get<std::string>()
                : json["error"].dump();
            return response;
        }
        if (json.contains("choices") && !json["choices"].empty()) {
            auto& choice = json["choices"][0];
            if (choice.contains("message")) {
                auto& msg = choice["message"];
                if (msg.contains("content") && !msg["content"].is_null()) {
                    response.content = msg["content"].get<std::string>();
                }
                if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                    for (const auto& tc : msg["tool_calls"]) {
                        ToolCall call;
                        call.id = tc.value("id", "");
                        call.type = tc.value("type", "function");
                        if (tc.contains("function")) {
                            call.name = tc["function"].value("name", "");
                            std::string args =
                                tc["function"].value("arguments", "{}");
                            try {
                                call.arguments = nlohmann::json::parse(args);
                            } catch (const std::exception& e) {
                                call.arguments_error =
                                    std::string("Malformed tool arguments: ")
                                    + e.what();
                            }
                        }
                        response.tool_calls.push_back(std::move(call));
                    }
                }
            }
            if (json.contains("usage")) {
                response.input_tokens =
                    json["usage"].value("prompt_tokens", int64_t{0});
                response.output_tokens =
                    json["usage"].value("completion_tokens", int64_t{0});
            }
        }
        response.has_tool_calls = !response.tool_calls.empty();
        response.success =
            !response.content.empty() || response.has_tool_calls;
    } catch (const std::exception& e) {
        response.error_message =
            std::string("JSON parse error: ") + e.what();
    }
    return response;
}

// ─── Anthropic-format helpers ──────────────────────────────────────────────

nlohmann::json anthropicBody(
    const std::string& model_name,
    const std::vector<ChatMessage>& messages,
    const std::vector<nlohmann::json>& tools,
    bool stream
) {
    nlohmann::json body;
    body["model"] = model_name;
    body["max_tokens"] = 4096;
    body["stream"] = stream;

    std::string system_text;
    for (const auto& msg : messages) {
        if (msg.role == "system") {
            system_text = msg.content;
        } else {
            nlohmann::json m{{"role", msg.role}};
            if (!msg.content_blocks.is_null()) {
                m["content"] = msg.content_blocks;
            } else if (msg.role == "assistant" && !msg.tool_calls.empty()) {
                nlohmann::json blocks = nlohmann::json::array();
                if (!msg.content.empty()) {
                    blocks.push_back(
                        {{"type", "text"}, {"text", msg.content}}
                    );
                }
                for (const auto& call : msg.tool_calls) {
                    blocks.push_back({
                        {"type", "tool_use"},
                        {"id", call.id},
                        {"name", call.name},
                        {"input", call.input.is_null()
                            ? call.arguments
                            : call.input}
                    });
                }
                m["content"] = std::move(blocks);
            } else {
                m["content"] = msg.content;
            }
            body["messages"].push_back(std::move(m));
        }
    }

    if (!system_text.empty()) {
        body["system"] = system_text;
    }

    if (!tools.empty()) {
        body["tools"] = nlohmann::json::array();
        for (const auto& tool : tools) {
            if (tool.contains("function")) {
                body["tools"].push_back({
                    {"name", tool["function"].value("name", "")},
                    {"description",
                     tool["function"].value("description", "")},
                    {"input_schema",
                     tool["function"].value(
                         "parameters", nlohmann::json::object()
                     )}
                });
            }
        }
    }
    return body;
}

std::string anthropicTextDelta(const std::string& event) {
    try {
        const auto payload = nlohmann::json::parse(event);
        if (payload.value("type", "") == "content_block_delta") {
            const auto delta = payload.value("delta", nlohmann::json::object());
            if (delta.value("type", "") == "text_delta") {
                return delta.value("text", "");
            }
        }
    } catch (...) {}
    return "";
}

ChatResponse parseAnthropicStream(const std::vector<std::string>& events) {
    ChatResponse response;
    response.input_tokens = 0;
    response.output_tokens = 0;
    response.success = false;

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
            response.input_tokens =
                payload["message"]
                    .value("usage", nlohmann::json::object())
                    .value("input_tokens", int64_t{0});
        } else if (type == "message_delta") {
            response.output_tokens =
                payload.value("usage", nlohmann::json::object())
                    .value("output_tokens", response.output_tokens);
        } else if (type == "content_block_start") {
            const std::size_t index = payload.value("index", std::size_t{0});
            const auto block =
                payload.value("content_block", nlohmann::json::object());
            if (block.value("type", "") == "tool_use") {
                auto& call = tool_calls[index];
                call.id = block.value("id", "");
                call.name = block.value("name", "");
                call.input = block.value("input", nlohmann::json::object());
            }
        } else if (type == "content_block_delta") {
            const std::size_t index = payload.value("index", std::size_t{0});
            const auto delta =
                payload.value("delta", nlohmann::json::object());
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
            } catch (const std::exception& e) {
                call.arguments_error =
                    std::string("Malformed tool arguments: ") + e.what();
            }
        }
        response.tool_calls.push_back(std::move(call));
    }
    response.has_tool_calls = !response.tool_calls.empty();
    response.success =
        !response.content.empty() || response.has_tool_calls;
    return response;
}

ChatResponse parseAnthropicNonStreaming(const std::string& body) {
    ChatResponse response;
    response.success = false;
    try {
        auto json = nlohmann::json::parse(body);
        if (json.contains("error")) {
            response.error_message = json["error"].is_string()
                ? json["error"].get<std::string>()
                : json["error"].dump();
            return response;
        }
        if (json.contains("content") && json["content"].is_array()) {
            response.content_blocks = json["content"];
            for (const auto& block : json["content"]) {
                std::string bt = block.value("type", "");
                if (bt == "text" && block.contains("text") &&
                    block["text"].is_string()) {
                    if (!response.content.empty()) response.content += "\n";
                    response.content += block["text"].get<std::string>();
                } else if (bt == "tool_use") {
                    ToolCall call;
                    call.id = block.value("id", "");
                    call.name = block.value("name", "");
                    call.input = block.contains("input")
                        ? block["input"]
                        : nlohmann::json::object();
                    response.tool_calls.push_back(std::move(call));
                }
            }
            if (json.contains("usage")) {
                response.input_tokens =
                    json["usage"].value("input_tokens", int64_t{0});
                response.output_tokens =
                    json["usage"].value("output_tokens", int64_t{0});
            }
        }
        response.has_tool_calls = !response.tool_calls.empty();
        response.success =
            !response.content.empty() || response.has_tool_calls;
    } catch (const std::exception& e) {
        response.error_message =
            std::string("JSON parse error: ") + e.what();
    }
    return response;
}

} // anonymous namespace

// ─── HttpModelProvider ─────────────────────────────────────────────────────

HttpModelProvider::HttpModelProvider(
    std::string provider_name,
    HttpClient& client,
    std::string endpoint,
    std::string api_key,
    std::string model,
    ModelCapabilities capabilities
) : provider_name_(std::move(provider_name)),
    client_(client),
    endpoint_(std::move(endpoint)),
    api_key_(std::move(api_key)),
    model_(std::move(model)),
    capabilities_(capabilities) {}

std::string HttpModelProvider::name() const { return provider_name_; }
std::string HttpModelProvider::model() const { return model_; }
ModelCapabilities HttpModelProvider::capabilities() const {
    return capabilities_;
}

ChatResponse HttpModelProvider::request(
    const std::vector<ChatMessage>& messages,
    const std::vector<nlohmann::json>& tools,
    const std::function<void(const std::string&)>& on_delta
) {
    const bool stream = (on_delta != nullptr);
    const nlohmann::json body = buildRequestBody(messages, tools, stream);
    const auto headers = requestHeaders();

    RawHttpResult raw;
    if (stream) {
        std::vector<std::string> collected;
        raw = client_.sendRaw(
            endpoint_, body.dump(), headers,
            [&](const std::string& event) {
                collected.push_back(event);
                const std::string text = extractTextDelta(event);
                if (!text.empty()) on_delta(text);
            }
        );
        if (!raw.success) {
            return errorResponse(raw.error_message, raw.error_category);
        }
        return parseStreamingResponse(raw.sse_events);
    }

    raw = client_.sendRawNonStreaming(endpoint_, body.dump(), headers);
    if (!raw.success) {
        return errorResponse(raw.error_message, raw.error_category);
    }
    return parseNonStreamingResponse(raw.raw_body);
}

void HttpModelProvider::appendToolResults(
    std::vector<ChatMessage>& messages,
    const std::vector<ToolCall>& calls,
    const std::vector<nlohmann::json>& results
) const {
    appendToolResultsImpl(messages, calls, results);
}

// ─── OpenAI provider ───────────────────────────────────────────────────────

namespace {

ModelCapabilities openAICapabilities() {
    ModelCapabilities v;
    v.image_input = true;
    return v;
}

class OpenAIProvider final : public HttpModelProvider {
public:
    OpenAIProvider(HttpClient& client, std::string endpoint,
                   std::string key, std::string model)
        : HttpModelProvider("OpenAI", client, std::move(endpoint),
                            std::move(key), std::move(model),
                            openAICapabilities()) {}

protected:
    nlohmann::json buildRequestBody(
        const std::vector<ChatMessage>& messages,
        const std::vector<nlohmann::json>& tools,
        bool stream
    ) const override {
        return openAIBody(model(), messages, tools, stream);
    }

    std::string extractTextDelta(
        const std::string& event
    ) const override {
        return openAITextDelta(event);
    }

    ChatResponse parseStreamingResponse(
        const std::vector<std::string>& events
    ) const override {
        return parseOpenAIStream(events);
    }

    ChatResponse parseNonStreamingResponse(
        const std::string& body
    ) const override {
        return parseOpenAINonStreaming(body);
    }

    std::vector<HttpClient::Header> requestHeaders() const override {
        return {{"Content-Type", "application/json"},
                {"Authorization", "Bearer " + apiKey()}};
    }

    void appendToolResultsImpl(
        std::vector<ChatMessage>& messages,
        const std::vector<ToolCall>& calls,
        const std::vector<nlohmann::json>& results
    ) const override {
        for (std::size_t i = 0; i < calls.size(); ++i) {
            messages.push_back({
                "tool", resultContent(results[i]), nullptr, calls[i].id, {}
            });
        }
    }
};

// ─── Anthropic provider ────────────────────────────────────────────────────

ModelCapabilities anthropicCapabilities() {
    ModelCapabilities v;
    v.context_window = 200000;
    v.image_input = true;
    return v;
}

class AnthropicProvider final : public HttpModelProvider {
public:
    AnthropicProvider(HttpClient& client, std::string endpoint,
                      std::string key, std::string model)
        : HttpModelProvider("Anthropic", client, std::move(endpoint),
                            std::move(key), std::move(model),
                            anthropicCapabilities()) {}

protected:
    nlohmann::json buildRequestBody(
        const std::vector<ChatMessage>& messages,
        const std::vector<nlohmann::json>& tools,
        bool stream
    ) const override {
        return anthropicBody(model(), messages, tools, stream);
    }

    std::string extractTextDelta(
        const std::string& event
    ) const override {
        return anthropicTextDelta(event);
    }

    ChatResponse parseStreamingResponse(
        const std::vector<std::string>& events
    ) const override {
        return parseAnthropicStream(events);
    }

    ChatResponse parseNonStreamingResponse(
        const std::string& body
    ) const override {
        return parseAnthropicNonStreaming(body);
    }

    std::vector<HttpClient::Header> requestHeaders() const override {
        return {{"Content-Type", "application/json"},
                {"x-api-key", apiKey()},
                {"anthropic-version", "2023-06-01"}};
    }

    void appendToolResultsImpl(
        std::vector<ChatMessage>& messages,
        const std::vector<ToolCall>& calls,
        const std::vector<nlohmann::json>& results
    ) const override {
        nlohmann::json blocks = nlohmann::json::array();
        for (std::size_t i = 0; i < calls.size(); ++i) {
            blocks.push_back({
                {"type", "tool_result"},
                {"tool_use_id", calls[i].id},
                {"content", resultContent(results[i])},
                {"is_error", !results[i].value("success", false)}
            });
        }
        messages.push_back({"user", "", std::move(blocks), "", {}});
    }
};

// ─── DeepSeek provider ─────────────────────────────────────────────────────

ModelCapabilities deepSeekCapabilities() {
    ModelCapabilities v;
    v.context_window = 128000;
    v.image_input = false;
    return v;
}

class DeepSeekProvider final : public HttpModelProvider {
public:
    DeepSeekProvider(HttpClient& client, std::string endpoint,
                     std::string key, std::string model)
        : HttpModelProvider("DeepSeek", client, std::move(endpoint),
                            std::move(key), std::move(model),
                            deepSeekCapabilities()) {}

protected:
    nlohmann::json buildRequestBody(
        const std::vector<ChatMessage>& messages,
        const std::vector<nlohmann::json>& tools,
        bool stream
    ) const override {
        return openAIBody(model(), messages, tools, stream);
    }

    std::string extractTextDelta(
        const std::string& event
    ) const override {
        return openAITextDelta(event);
    }

    ChatResponse parseStreamingResponse(
        const std::vector<std::string>& events
    ) const override {
        return parseOpenAIStream(events);
    }

    ChatResponse parseNonStreamingResponse(
        const std::string& body
    ) const override {
        return parseOpenAINonStreaming(body);
    }

    std::vector<HttpClient::Header> requestHeaders() const override {
        return {{"Content-Type", "application/json"},
                {"Authorization", "Bearer " + apiKey()}};
    }

    void appendToolResultsImpl(
        std::vector<ChatMessage>& messages,
        const std::vector<ToolCall>& calls,
        const std::vector<nlohmann::json>& results
    ) const override {
        for (std::size_t i = 0; i < calls.size(); ++i) {
            messages.push_back({
                "tool", resultContent(results[i]), nullptr, calls[i].id, {}
            });
        }
    }
};

// ─── OpenAI-compatible provider ────────────────────────────────────────────

ModelCapabilities compatibleCapabilities() {
    ModelCapabilities v;
    v.context_window = 32768;
    v.streaming_usage = false;
    v.image_input = false;
    return v;
}

class OpenAICompatibleProvider final : public HttpModelProvider {
public:
    OpenAICompatibleProvider(std::string name, HttpClient& client,
                             std::string endpoint, std::string key,
                             std::string model)
        : HttpModelProvider(std::move(name), client, std::move(endpoint),
                            std::move(key), std::move(model),
                            compatibleCapabilities()) {}

protected:
    nlohmann::json buildRequestBody(
        const std::vector<ChatMessage>& messages,
        const std::vector<nlohmann::json>& tools,
        bool stream
    ) const override {
        return openAIBody(model(), messages, tools, stream);
    }

    std::string extractTextDelta(
        const std::string& event
    ) const override {
        return openAITextDelta(event);
    }

    ChatResponse parseStreamingResponse(
        const std::vector<std::string>& events
    ) const override {
        return parseOpenAIStream(events);
    }

    ChatResponse parseNonStreamingResponse(
        const std::string& body
    ) const override {
        return parseOpenAINonStreaming(body);
    }

    std::vector<HttpClient::Header> requestHeaders() const override {
        std::vector<HttpClient::Header> h{
            {"Content-Type", "application/json"}
        };
        if (!apiKey().empty()) {
            h.emplace_back("Authorization", "Bearer " + apiKey());
        }
        return h;
    }

    void appendToolResultsImpl(
        std::vector<ChatMessage>& messages,
        const std::vector<ToolCall>& calls,
        const std::vector<nlohmann::json>& results
    ) const override {
        for (std::size_t i = 0; i < calls.size(); ++i) {
            messages.push_back({
                "tool", resultContent(results[i]), nullptr, calls[i].id, {}
            });
        }
    }
};

} // anonymous namespace

// ─── factory ───────────────────────────────────────────────────────────────

std::unique_ptr<ModelProvider> createModelProvider(
    const std::string& provider,
    HttpClient& client,
    std::string endpoint,
    std::string api_key,
    std::string model
) {
    if (provider == "OpenAI") {
        return std::make_unique<OpenAIProvider>(
            client, std::move(endpoint), std::move(api_key), std::move(model)
        );
    }
    if (provider == "Anthropic") {
        return std::make_unique<AnthropicProvider>(
            client, std::move(endpoint), std::move(api_key), std::move(model)
        );
    }
    if (provider == "DeepSeek") {
        return std::make_unique<DeepSeekProvider>(
            client, std::move(endpoint), std::move(api_key), std::move(model)
        );
    }
    if (provider == "LlamaCpp" || provider == "OpenAICompatible") {
        return std::make_unique<OpenAICompatibleProvider>(
            provider, client, std::move(endpoint), std::move(api_key),
            std::move(model)
        );
    }
    throw std::invalid_argument("Unsupported model provider: " + provider);
}

} // namespace opencode
