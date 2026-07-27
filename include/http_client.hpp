#ifndef HTTP_CLIENT_HPP
#define HTTP_CLIENT_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <nlohmann/json.hpp>
#include "error_category.hpp"
#include "cancellation.hpp"

namespace opencode {

struct ToolCall {
    std::string id;
    std::string type;
    std::string name;
    nlohmann::json arguments;       // OpenAI format: function.arguments
    nlohmann::json input;           // Anthropic format: tool_use.input
    std::string arguments_error;    // non-empty when arguments JSON failed to parse
};

struct ChatMessage {
    std::string role;  // "system", "user", "assistant", "tool"
    std::string content;
    nlohmann::json content_blocks = nullptr;  // For Anthropic multi-block content
    std::string tool_call_id;  // For tool messages (OpenAI format)
    std::vector<ToolCall> tool_calls;  // For assistant messages with tool calls
    std::vector<std::string> image_urls = {};  // data: URLs or https: URLs for vision
};

struct ChatResponse {
    std::string content;
    std::vector<ToolCall> tool_calls;
    int64_t input_tokens;
    int64_t output_tokens;
    bool success;
    std::string error_message;
    ErrorCategory error_category = ErrorCategory::None;
    nlohmann::json content_blocks;  // For Anthropic response parsing
    bool has_tool_calls = false;    // 标记是否有工具调用需要执行
};

/// Result from a raw HTTP streaming request.
struct RawHttpResult {
    std::vector<std::string> sse_events;
    std::string raw_body;
    long http_code = 0;
    bool success = false;
    bool output_delivered = false;
    std::string error_message;
    ErrorCategory error_category = ErrorCategory::None;
};

/// Low-level HTTP transport. Provider-specific encoding/decoding is the
/// responsibility of ModelProvider subclasses.
class HttpClient {
public:
    using Header = std::pair<std::string, std::string>;

    HttpClient();
    void setCancellationToken(CancellationToken* token);

    /// Send a raw streaming HTTP POST. The body is sent as-is; SSE framing is
    /// parsed and each event is forwarded to @p on_sse_event. Returns collected
    /// events and transport status. Retries are handled internally.
    RawHttpResult sendRaw(
        const std::string& url,
        const std::string& body,
        const std::vector<Header>& headers,
        const std::function<void(const std::string&)>& on_sse_event = nullptr
    );

    /// Convenience: send raw non-streaming request.
    RawHttpResult sendRawNonStreaming(
        const std::string& url,
        const std::string& body,
        const std::vector<Header>& headers
    );

    // Legacy — prefer sendRaw(). These delegate to sendRaw internally.
    // @deprecated
    ChatResponse sendChatRequestStreaming(
        const std::string& url,
        const std::string& api_key,
        const std::string& model,
        const std::vector<ChatMessage>& messages,
        const std::string& provider_type,
        const std::vector<nlohmann::json>& tools = {},
        std::function<void(const std::string&)> onChunk = nullptr
    );
    ChatResponse sendChatRequest(
        const std::string& url,
        const std::string& api_key,
        const std::string& model,
        const std::vector<ChatMessage>& messages,
        const std::string& provider_type,
        const std::vector<nlohmann::json>& tools = {}
    );

    bool validateApiKey(const std::string& url, const std::string& api_key, const std::string& model);

private:
    CancellationToken* cancellation_token_ = nullptr;

    std::string performCurlRequest(
        const std::string& url,
        const std::string& data,
        const std::vector<Header>& headers,
        std::size_t retry_count = 0
    );
    std::string performCurlRequestStreaming(
        const std::string& url,
        const std::string& data,
        const std::vector<Header>& headers,
        const std::function<void(const std::string&)>& on_sse_event,
        std::size_t retry_count = 0
    );

    // Legacy helpers — will be removed once sendChatRequestStreaming is gone.
    nlohmann::json buildRequestBody(
        const std::string& model,
        const std::vector<ChatMessage>& messages,
        const std::string& provider_type,
        const std::vector<nlohmann::json>& tools,
        bool stream = false
    );
};

} // namespace opencode

#endif // HTTP_CLIENT_HPP
