#include "http_client.hpp"
#include <curl/curl.h>
#include <iostream>
#include <functional>

namespace opencode {

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// 流式写入回调结构体
struct StreamCallbackData {
    std::function<void(const std::string&)> onChunk;
    std::string accumulated_content;
};

static size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    StreamCallbackData* cb_data = static_cast<StreamCallbackData*>(userp);
    
    std::string chunk(static_cast<char*>(contents), total_size);
    cb_data->accumulated_content += chunk;
    
    // 处理 SSE 格式 (data: {...}\n\n)
    size_t pos = 0;
    while ((pos = cb_data->accumulated_content.find("\n\n")) != std::string::npos) {
        std::string line = cb_data->accumulated_content.substr(0, pos);
        cb_data->accumulated_content.erase(0, pos + 2);
        
        if (line.rfind("data: ", 0) == 0) {
            std::string data = line.substr(6);
            if (data == "[DONE]") {
                break;
            }
            if (cb_data->onChunk) {
                cb_data->onChunk(data);
            }
        }
    }
    
    return total_size;
}

HttpClient::HttpClient() {
    curl_global_init(CURL_GLOBAL_ALL);
}

std::string HttpClient::performCurlRequest(const std::string& url, const std::string& data, const std::string& api_key, const std::string& provider_type) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return "{\"error\": \"Failed to initialize CURL\"}";
    }
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    if (!api_key.empty()) {
        if (provider_type == "Anthropic") {
            std::string api_key_header = "x-api-key: " + api_key;
            headers = curl_slist_append(headers, api_key_header.c_str());
            headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
        } else {
            std::string auth_header = "Authorization: Bearer " + api_key;
            headers = curl_slist_append(headers, auth_header.c_str());
        }
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

std::string HttpClient::performCurlRequestStreaming(const std::string& url, const std::string& data, const std::string& api_key, const std::string& provider_type, std::function<void(const std::string&)> onChunk) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return "{\"error\": \"Failed to initialize CURL\"}";
    }
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    if (!api_key.empty()) {
        if (provider_type == "Anthropic") {
            std::string api_key_header = "x-api-key: " + api_key;
            headers = curl_slist_append(headers, api_key_header.c_str());
            headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
        } else {
            std::string auth_header = "Authorization: Bearer " + api_key;
            headers = curl_slist_append(headers, auth_header.c_str());
        }
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
    
    StreamCallbackData cb_data;
    cb_data.onChunk = onChunk;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &cb_data);
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
    
    return cb_data.accumulated_content;
}

nlohmann::json HttpClient::buildRequestBody(const std::string& model, const std::vector<ChatMessage>& messages, const std::string& provider_type, const std::vector<nlohmann::json>& tools, bool stream) {
    nlohmann::json body;
    
    // DeepSeek/OpenAI/兼容格式
    if (provider_type == "DeepSeek" || provider_type == "OpenAI" || provider_type == "Unknown") {
        body["model"] = model;
        body["messages"] = nlohmann::json::array();
        
        for (const auto& msg : messages) {
            nlohmann::json message = {
                {"role", msg.role},
                {"content", msg.content}
            };
            // 对于 tool 消息，添加 tool_call_id
            if (msg.role == "tool" && !msg.tool_call_id.empty()) {
                message["tool_call_id"] = msg.tool_call_id;
            }
            body["messages"].push_back(message);
        }
        
        body["stream"] = stream;
        body["temperature"] = 0.7;
        body["max_tokens"] = 4096;
        if (!tools.empty()) {
            body["tools"] = tools;
        }
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
                nlohmann::json message = {{"role", msg.role}};
                if (!msg.content_blocks.is_null()) {
                    message["content"] = msg.content_blocks;
                } else {
                    message["content"] = msg.content;
                }
                body["messages"].push_back(message);
            }
        }
        
        if (!system_content.empty()) {
            body["system"] = system_content;
        }
        
        if (!tools.empty()) {
            body["tools"] = nlohmann::json::array();
            for (const auto& tool : tools) {
                if (tool.contains("function")) {
                    body["tools"].push_back({
                        {"name", tool["function"].value("name", "")},
                        {"description", tool["function"].value("description", "")},
                        {"input_schema", tool["function"].value("parameters", nlohmann::json::object())}
                    });
                }
            }
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
        
        body["stream"] = stream;
    }
    
    return body;
}

ChatResponse HttpClient::sendChatRequest(
    const std::string& url,
    const std::string& api_key,
    const std::string& model,
    const std::vector<ChatMessage>& messages,
    const std::string& provider_type,
    const std::vector<nlohmann::json>& tools
) {
    return sendChatRequestStreaming(url, api_key, model, messages, provider_type, tools, nullptr);
}

ChatResponse HttpClient::sendChatRequestStreaming(
    const std::string& url,
    const std::string& api_key,
    const std::string& model,
    const std::vector<ChatMessage>& messages,
    const std::string& provider_type,
    const std::vector<nlohmann::json>& tools,
    std::function<void(const std::string&)> onChunk
) {
    ChatResponse response;
    response.success = false;
    response.input_tokens = 0;
    response.output_tokens = 0;
    
    bool stream = (onChunk != nullptr);
    nlohmann::json body = buildRequestBody(model, messages, provider_type, tools, stream);
    
    std::string result;
    if (stream) {
        result = performCurlRequestStreaming(url, body.dump(), api_key, provider_type, onChunk);
    } else {
        result = performCurlRequest(url, body.dump(), api_key, provider_type);
    }
    
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
            if (choice.contains("message")) {
                auto& message = choice["message"];
                
                // 解析文本内容
                if (message.contains("content") && !message["content"].is_null()) {
                    response.content = message["content"].get<std::string>();
                }
                
                // 解析原生 tool_calls (OpenAI/DeepSeek 格式)
                if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
                    for (const auto& tc : message["tool_calls"]) {
                        ToolCall call;
                        call.id = tc.value("id", "");
                        call.type = tc.value("type", "function");
                        if (tc.contains("function")) {
                            call.name = tc["function"].value("name", "");
                            std::string args_str = tc["function"].value("arguments", "{}");
                            try {
                                call.arguments = nlohmann::json::parse(args_str);
                            } catch (...) {
                                call.arguments = nlohmann::json::object();
                            }
                        }
                        response.tool_calls.push_back(call);
                    }
                }
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
        else if (json_result.contains("content") && json_result["content"].is_array()) {
            response.content_blocks = json_result["content"];
            for (const auto& content : json_result["content"]) {
                std::string type = content.value("type", "");
                if (type == "text" && content.contains("text") && content["text"].is_string()) {
                    if (!response.content.empty()) {
                        response.content += "\n";
                    }
                    response.content += content["text"].get<std::string>();
                } else if (type == "tool_use") {
                    ToolCall call;
                    call.id = content.value("id", "");
                    call.name = content.value("name", "");
                    call.input = content.contains("input") ? content["input"] : nlohmann::json::object();
                    response.tool_calls.push_back(call);
                }
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
        
        response.has_tool_calls = !response.tool_calls.empty();
        response.success = !response.content.empty() || !response.tool_calls.empty();
        
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
