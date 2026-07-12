#ifndef HTTP_CLIENT_HPP
#define HTTP_CLIENT_HPP

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace opencode {

struct ChatMessage {
    std::string role;  // "system", "user", "assistant"
    std::string content;
};

struct ToolCall {
    std::string id;
    std::string type;
    std::string name;
    nlohmann::json arguments;
};

struct ChatResponse {
    std::string content;
    std::vector<ToolCall> tool_calls;
    int64_t input_tokens;
    int64_t output_tokens;
    bool success;
    std::string error_message;
};

class HttpClient {
public:
    HttpClient();
    
    // 发送聊天请求 (支持多提供商)
    ChatResponse sendChatRequest(
        const std::string& url,
        const std::string& api_key,
        const std::string& model,
        const std::vector<ChatMessage>& messages,
        const std::string& provider_type
    );
    
    // 验证 API Key
    bool validateApiKey(const std::string& url, const std::string& api_key, const std::string& model);
    
private:
    std::string performCurlRequest(const std::string& url, const std::string& data, const std::string& api_key);
    nlohmann::json buildRequestBody(const std::string& model, const std::vector<ChatMessage>& messages, const std::string& provider_type);
};

} // namespace opencode

#endif // HTTP_CLIENT_HPP
