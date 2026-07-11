#include "http_client.hpp"
#include <curl/curl.h>
#include <iostream>

namespace opencode {

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

HttpClient::HttpClient() {
    curl_global_init(CURL_GLOBAL_ALL);
}

std::string HttpClient::performCurlRequest(const std::string& url, const std::string& data, const std::string& api_key) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return "{\"error\": \"Failed to initialize CURL\"}";
    }
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    if (!api_key.empty()) {
        std::string auth_header = "Authorization: Bearer " + api_key;
        headers = curl_slist_append(headers, auth_header.c_str());
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    
    std::string response_data;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    
    // 禁用 SSL 验证 (仅用于本地测试，生产环境应启用)
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        return "{\"error\": \"" + std::string(curl_easy_strerror(res)) + "\"}";
    }
    
    return response_data;
}

nlohmann::json HttpClient::buildRequestBody(const std::string& model, const std::vector<ChatMessage>& messages, const std::string& provider_type) {
    nlohmann::json body;
    
    // DeepSeek/OpenAI/兼容格式
    if (provider_type == "DeepSeek" || provider_type == "OpenAI" || provider_type == "Unknown") {
        body["model"] = model;
        body["messages"] = nlohmann::json::array();
        
        for (const auto& msg : messages) {
            body["messages"].push_back({
                {"role", msg.role},
                {"content", msg.content}
            });
        }
        
        body["stream"] = false;
        body["temperature"] = 0.7;
        body["max_tokens"] = 4096;
    }
    // Anthropic 格式
    else if (provider_type == "Anthropic") {
        body["model"] = model;
        body["max_tokens"] = 4096;
        
        // 分离 system 消息
        std::string system_content;
        for (const auto& msg : messages) {
            if (msg.role == "system") {
                system_content = msg.content;
            } else {
                body["messages"].push_back({
                    {"role", msg.role},
                    {"content", msg.content}
                });
            }
        }
        
        if (!system_content.empty()) {
            body["system"] = system_content;
        }
    }
    // LlamaCpp/Ollama 格式
    else if (provider_type == "LlamaCpp") {
        body["model"] = model;
        body["messages"] = nlohmann::json::array();
        
        for (const auto& msg : messages) {
            body["messages"].push_back({
                {"role", msg.role},
                {"content", msg.content}
            });
        }
        
        body["stream"] = false;
    }
    
    return body;
}

ChatResponse HttpClient::sendChatRequest(
    const std::string& url,
    const std::string& api_key,
    const std::string& model,
    const std::vector<ChatMessage>& messages,
    const std::string& provider_type
) {
    ChatResponse response;
    response.success = false;
    response.input_tokens = 0;
    response.output_tokens = 0;
    
    nlohmann::json body = buildRequestBody(model, messages, provider_type);
    std::string result = performCurlRequest(url, body.dump(), api_key);
    
    try {
        nlohmann::json json_result = nlohmann::json::parse(result);
        
        // 检查错误
        if (json_result.contains("error")) {
            response.error_message = json_result["error"].is_string() ? 
                json_result["error"].get<std::string>() : json_result["error"].dump();
            return response;
        }
        
        // 解析响应 (OpenAI/DeepSeek 格式)
        if (json_result.contains("choices") && !json_result["choices"].empty()) {
            auto& choice = json_result["choices"][0];
            if (choice.contains("message") && choice["message"].contains("content")) {
                response.content = choice["message"]["content"].get<std::string>();
            }
            
            if (json_result.contains("usage")) {
                if (json_result["usage"].contains("prompt_tokens")) {
                    response.input_tokens = json_result["usage"]["prompt_tokens"].get<int64_t>();
                }
                if (json_result["usage"].contains("completion_tokens")) {
                    response.output_tokens = json_result["usage"]["completion_tokens"].get<int64_t>();
                }
            }
        }
        // Anthropic 格式
        else if (json_result.contains("content") && !json_result["content"].empty()) {
            auto& content = json_result["content"][0];
            if (content.contains("text")) {
                response.content = content["text"].get<std::string>();
            }
            
            if (json_result.contains("usage")) {
                if (json_result["usage"].contains("input_tokens")) {
                    response.input_tokens = json_result["usage"]["input_tokens"].get<int64_t>();
                }
                if (json_result["usage"].contains("output_tokens")) {
                    response.output_tokens = json_result["usage"]["output_tokens"].get<int64_t>();
                }
            }
        }
        
        response.success = !response.content.empty();
        
    } catch (const std::exception& e) {
        response.error_message = "JSON parse error: " + std::string(e.what());
        response.content = result;  // 返回原始响应以便调试
    }
    
    return response;
}

bool HttpClient::validateApiKey(const std::string& url, const std::string& api_key, const std::string& model) {
    std::vector<ChatMessage> test_messages = {
        {"user", "Hello"}
    };
    
    ChatResponse response = sendChatRequest(url, api_key, model, test_messages, "Unknown");
    return response.success;
}

} // namespace opencode
