#ifndef HTTP_CLIENT_HPP
#define HTTP_CLIENT_HPP

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace opencode {

struct ToolCall {
    std::string id;
    std::string type;
    std::string name;
    nlohmann::json arguments;  // OpenAI format: function.arguments
    nlohmann::json input;      // Anthropic format: tool_use.input
};

struct ChatMessage {
    std::string role;  // "system", "user", "assistant", "tool"
    std::string content;
    nlohmann::json content_blocks = nullptr;  // For Anthropic multi-block content
    std::string tool_call_id;  // For tool messages (OpenAI format)
    std::vector<ToolCall> tool_calls;  // For assistant messages with tool calls
};

struct ChatResponse {
    std::string content;
    std::vector<ToolCall> tool_calls;
    int64_t input_tokens;
    int64_t output_tokens;
    bool success;
    std::string error_message;
    nlohmann::json content_blocks;  // For Anthropic response parsing
    bool has_tool_calls = false;    // 标记是否有工具调用需要执行
};

class HttpClient {
public:
    HttpClient();
    
    // 发送聊天请求 (支持多提供商) - 流式版本
    ChatResponse sendChatRequestStreaming(
        const std::string& url,
        const std::string& api_key,
        const std::string& model,
        const std::vector<ChatMessage>& messages,
        const std::string& provider_type,
        const std::vector<nlohmann::json>& tools = {},
        std::function<void(const std::string&)> onChunk = nullptr
    );
    
    // 发送聊天请求 (支持多提供商) - 非流式版本 (保留向后兼容)
    ChatResponse sendChatRequest(
        const std::string& url,
        const std::string& api_key,
        const std::string& model,
        const std::vector<ChatMessage>& messages,
        const std::string& provider_type,
        const std::vector<nlohmann::json>& tools = {}
    );
    
    // 验证 API Key
    bool validateApiKey(const std::string& url, const std::string& api_key, const std::string& model);

private:
    std::string performCurlRequest(const std::string& url, const std::string& data, const std::string& api_key, const std::string& provider_type);
    std::string performCurlRequestStreaming(const std::string& url, const std::string& data, const std::string& api_key, const std::string& provider_type, std::function<void(const std::string&)> onChunk);
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
