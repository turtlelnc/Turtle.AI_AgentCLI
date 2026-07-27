#include "http_client.hpp"
#include "stream_parser.hpp"
#include "endpoint_policy.hpp"
#include "http_retry_policy.hpp"
#include <curl/curl.h>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <functional>
#include <thread>
#include <ctime>

namespace opencode {

namespace {

std::string makeErrorResponse(
    const std::string& message,
    ErrorCategory category = ErrorCategory::Provider
) {
    return nlohmann::json({
        {"error", message},
        {"error_category", errorCategoryName(category)}
    }).dump();
}

void configureCommonCurlOptions(CURL* curl) {
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(
        curl,
        CURLOPT_FOLLOWLOCATION,
        EndpointPolicy::allowHttpRedirects() ? 1L : 0L
    );
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 0L);
}

std::string curlErrorResponse(CURLcode result, ErrorCategory category) {
    return makeErrorResponse(std::string("Network request failed: ") +
                           curl_easy_strerror(result), category);
}

std::string httpErrorResponse(
    long status,
    const std::string& response_body,
    ErrorCategory category
) {
    std::string message = "HTTP " + std::to_string(status);
    if (!response_body.empty()) {
        message += ": " + response_body;
    }
    return makeErrorResponse(message, category);
}

size_t HeaderCallback(
    char* buffer, size_t size, size_t nitems, void* userdata
) {
    const size_t total = size * nitems;
    std::string line(buffer, total);
    std::string prefix = "retry-after:";
    std::string lowered = line;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (lowered.rfind(prefix, 0) == 0) {
        try {
            const std::string value = line.substr(prefix.size());
            try {
                *static_cast<long*>(userdata) = std::stol(value) * 1000L;
            } catch (...) {
                const std::time_t when = curl_getdate(value.c_str(), nullptr);
                const std::time_t now = std::time(nullptr);
                if (when >= now) {
                    *static_cast<long*>(userdata) =
                        static_cast<long>(when - now) * 1000L;
                }
            }
        } catch (...) {}
    }
    return total;
}

int ProgressCallback(
    void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t
) {
    const auto* token = static_cast<const CancellationToken*>(userdata);
    return token && token->requested() ? 1 : 0;
}

bool waitForRetry(long delay_ms, const CancellationToken* cancellation) {
    long remaining = delay_ms;
    while (remaining > 0) {
        if (cancellation && cancellation->requested()) return false;
        const long slice = std::min<long>(remaining, 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(slice));
        remaining -= slice;
    }
    return !(cancellation && cancellation->requested());
}

} // namespace

// ─── CURL write callbacks ──────────────────────────────────────────────────

static size_t WriteCallback(
    void* contents, size_t size, size_t nmemb, std::string* userp
) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

struct StreamCallbackData {
    std::function<void(const std::string&)> on_sse_event;
    std::string pending_data;
    std::string raw_response;
    std::vector<std::string> events;
    bool delivered_output = false;
};

static size_t StreamWriteCallback(
    void* contents, size_t size, size_t nmemb, void* userp
) {
    size_t total_size = size * nmemb;
    auto* cb = static_cast<StreamCallbackData*>(userp);

    std::string chunk(static_cast<char*>(contents), total_size);
    cb->raw_response += chunk;
    cb->pending_data += chunk;

    // Normalize CRLF
    size_t crlf_pos = 0;
    while ((crlf_pos = cb->pending_data.find("\r\n", crlf_pos)) !=
           std::string::npos) {
        cb->pending_data.replace(crlf_pos, 2, "\n");
    }

    // Parse SSE blocks
    size_t pos = 0;
    while ((pos = cb->pending_data.find("\n\n")) != std::string::npos) {
        std::string block = cb->pending_data.substr(0, pos);
        cb->pending_data.erase(0, pos + 2);

        std::string data;
        size_t line_start = 0;
        while (line_start <= block.size()) {
            const size_t line_end = block.find('\n', line_start);
            const std::string line = block.substr(
                line_start,
                line_end == std::string::npos
                    ? std::string::npos
                    : line_end - line_start
            );
            if (line.rfind("data:", 0) == 0) {
                std::string value = line.substr(5);
                if (!value.empty() && value.front() == ' ') {
                    value.erase(0, 1);
                }
                if (!data.empty()) data += '\n';
                data += value;
            }
            if (line_end == std::string::npos) break;
            line_start = line_end + 1;
        }

        if (!data.empty() && data != "[DONE]") {
            cb->events.push_back(data);
            cb->delivered_output = true;
            if (cb->on_sse_event) {
                cb->on_sse_event(data);
            }
        }
    }

    return total_size;
}

// ─── HttpClient ────────────────────────────────────────────────────────────

HttpClient::HttpClient() {
    curl_global_init(CURL_GLOBAL_ALL);
}

void HttpClient::setCancellationToken(CancellationToken* token) {
    cancellation_token_ = token;
}

// ─── Raw (non-streaming) request ────────────────────────────────────────────

std::string HttpClient::performCurlRequest(
    const std::string& url,
    const std::string& data,
    const std::vector<Header>& headers,
    std::size_t retry_count
) {
    if (cancellation_token_ && cancellation_token_->requested()) {
        return makeErrorResponse("Request cancelled", ErrorCategory::Cancelled);
    }
    CURL* curl = curl_easy_init();
    if (!curl) {
        return "{\"error\": \"Failed to initialize CURL\"}";
    }

    struct curl_slist* curl_headers = nullptr;
    for (const auto& h : headers) {
        curl_headers = curl_slist_append(
            curl_headers, (h.first + ": " + h.second).c_str()
        );
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);

    std::string response_data;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    long retry_after_ms = -1;
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &retry_after_ms);
    configureCommonCurlOptions(curl);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancellation_token_);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(curl_headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code < 200 || http_code >= 300) {
        const RetryDecision decision = HttpRetryPolicy::assess(
            http_code, res, response_data, retry_count, retry_after_ms
        );
        if (decision.retry) {
            if (!waitForRetry(decision.delay_ms, cancellation_token_)) {
                return makeErrorResponse(
                    "Request cancelled during retry backoff",
                    ErrorCategory::Cancelled
                );
            }
            return performCurlRequest(url, data, headers, retry_count + 1);
        }
        if (res != CURLE_OK) {
            return curlErrorResponse(res, decision.category);
        }
        return httpErrorResponse(http_code, response_data, decision.category);
    }

    return response_data;
}

// ─── Raw streaming request ─────────────────────────────────────────────────

std::string HttpClient::performCurlRequestStreaming(
    const std::string& url,
    const std::string& data,
    const std::vector<Header>& headers,
    const std::function<void(const std::string&)>& on_sse_event,
    std::size_t retry_count
) {
    if (cancellation_token_ && cancellation_token_->requested()) {
        return makeErrorResponse("Request cancelled", ErrorCategory::Cancelled);
    }
    CURL* curl = curl_easy_init();
    if (!curl) {
        return "{\"error\": \"Failed to initialize CURL\"}";
    }

    struct curl_slist* curl_headers = nullptr;
    for (const auto& h : headers) {
        curl_headers = curl_slist_append(
            curl_headers, (h.first + ": " + h.second).c_str()
        );
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);

    StreamCallbackData cb_data;
    cb_data.on_sse_event = on_sse_event;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &cb_data);
    long retry_after_ms = -1;
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &retry_after_ms);
    configureCommonCurlOptions(curl);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancellation_token_);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(curl_headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code < 200 || http_code >= 300) {
        RetryDecision decision = HttpRetryPolicy::assess(
            http_code, res, cb_data.raw_response, retry_count, retry_after_ms
        );
        // Once any output has been delivered, do not retry.
        if (cb_data.delivered_output) decision.retry = false;
        if (decision.retry) {
            if (!waitForRetry(decision.delay_ms, cancellation_token_)) {
                return makeErrorResponse(
                    "Request cancelled during retry backoff",
                    ErrorCategory::Cancelled
                );
            }
            return performCurlRequestStreaming(
                url, data, headers, on_sse_event, retry_count + 1
            );
        }
        if (res != CURLE_OK) {
            return curlErrorResponse(res, decision.category);
        }
        return httpErrorResponse(
            http_code, cb_data.raw_response, decision.category
        );
    }

    return nlohmann::json({{"sse_events", cb_data.events}}).dump();
}

// ─── Public raw interface ──────────────────────────────────────────────────

RawHttpResult HttpClient::sendRaw(
    const std::string& url,
    const std::string& body,
    const std::vector<Header>& headers,
    const std::function<void(const std::string&)>& on_sse_event
) {
    RawHttpResult result;
    const std::string raw = performCurlRequestStreaming(
        url, body, headers, on_sse_event
    );

    try {
        auto json = nlohmann::json::parse(raw);
        if (json.contains("error")) {
            result.success = false;
            result.error_message = json["error"].is_string()
                ? json["error"].get<std::string>()
                : json["error"].dump();
            result.error_category = errorCategoryFromName(
                json.value("error_category", "provider_error")
            );
            return result;
        }
        if (json.contains("sse_events")) {
            result.sse_events =
                json["sse_events"].get<std::vector<std::string>>();
            result.output_delivered = !result.sse_events.empty();
        }
        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message =
            std::string("JSON parse error: ") + e.what();
        result.raw_body = raw;
    }
    return result;
}

RawHttpResult HttpClient::sendRawNonStreaming(
    const std::string& url,
    const std::string& body,
    const std::vector<Header>& headers
) {
    RawHttpResult result;
    result.raw_body = performCurlRequest(url, body, headers);

    try {
        auto json = nlohmann::json::parse(result.raw_body);
        if (json.contains("error")) {
            result.success = false;
            result.error_message = json["error"].is_string()
                ? json["error"].get<std::string>()
                : json["error"].dump();
            result.error_category = errorCategoryFromName(
                json.value("error_category", "provider_error")
            );
            return result;
        }
        result.success = true;
    } catch (const std::exception&) {
        // Non-JSON body is OK for some providers
        result.success = true;
    }
    return result;
}

// ─── Legacy (deprecated) ──────────────────────────────────────────────────

nlohmann::json HttpClient::buildRequestBody(
    const std::string& model,
    const std::vector<ChatMessage>& messages,
    const std::string& provider_type,
    const std::vector<nlohmann::json>& tools,
    bool stream
) {
    nlohmann::json body;

    if (provider_type == "DeepSeek" || provider_type == "OpenAI" ||
        provider_type == "OpenAICompatible" || provider_type == "Unknown") {
        body["model"] = model;
        body["messages"] = nlohmann::json::array();

        for (const auto& msg : messages) {
            nlohmann::json message = {
                {"role", msg.role},
                {"content", msg.content}
            };
            if (msg.role == "tool" && !msg.tool_call_id.empty()) {
                message["tool_call_id"] = msg.tool_call_id;
            }
            if (msg.role == "assistant" && !msg.tool_calls.empty()) {
                nlohmann::json tool_calls_json = nlohmann::json::array();
                for (const auto& tc : msg.tool_calls) {
                    tool_calls_json.push_back({
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
                message["tool_calls"] = std::move(tool_calls_json);
            }
            body["messages"].push_back(std::move(message));
        }

        body["stream"] = stream;
        body["temperature"] = 0.7;
        body["max_tokens"] = 4096;
        if (!tools.empty()) {
            body["tools"] = tools;
        }
    } else if (provider_type == "Anthropic") {
        body["model"] = model;
        body["max_tokens"] = 4096;
        body["stream"] = stream;

        std::string system_content;
        for (const auto& msg : messages) {
            if (msg.role == "system") {
                system_content = msg.content;
            } else {
                nlohmann::json message = {{"role", msg.role}};
                if (!msg.content_blocks.is_null()) {
                    message["content"] = msg.content_blocks;
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
                    message["content"] = std::move(blocks);
                } else {
                    message["content"] = msg.content;
                }
                body["messages"].push_back(std::move(message));
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
    } else if (provider_type == "LlamaCpp") {
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
    return sendChatRequestStreaming(
        url, api_key, model, messages, provider_type, tools, nullptr
    );
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

    const auto endpoint =
        EndpointPolicy::assess(provider_type, url, !api_key.empty());
    if (!endpoint.allowed) {
        response.error_message =
            "Endpoint policy rejected request: " + endpoint.error;
        return response;
    }

    // Build headers (provider-specific — legacy path)
    std::vector<Header> headers;
    headers.emplace_back("Content-Type", "application/json");
    if (!api_key.empty()) {
        if (provider_type == "Anthropic") {
            headers.emplace_back("x-api-key", api_key);
            headers.emplace_back("anthropic-version", "2023-06-01");
        } else {
            headers.emplace_back("Authorization", "Bearer " + api_key);
        }
    }

    bool stream = (onChunk != nullptr);
    nlohmann::json body =
        buildRequestBody(model, messages, provider_type, tools, stream);

    std::string result;
    if (stream) {
        std::function<void(const std::string&)> on_sse;
        if (onChunk) {
            // Legacy wrapper: extract visible text from SSE events using
            // the static helper from stream_parser / old visibleTextDelta.
            on_sse = [&onChunk, &provider_type](const std::string& event) {
                // Use the free-standing helper still in http_client.cpp
                // (moved to stream_parser or kept as local).
                try {
                    const auto payload = nlohmann::json::parse(event);
                    if (provider_type == "Anthropic") {
                        if (payload.value("type", "") ==
                            "content_block_delta") {
                            const auto delta = payload.value(
                                "delta", nlohmann::json::object()
                            );
                            if (delta.value("type", "") == "text_delta") {
                                onChunk(delta.value("text", ""));
                            }
                        }
                    } else if (payload.contains("choices") &&
                               !payload["choices"].empty()) {
                        const auto delta = payload["choices"][0].value(
                            "delta", nlohmann::json::object()
                        );
                        if (delta.contains("content") &&
                            delta["content"].is_string()) {
                            onChunk(delta["content"].get<std::string>());
                        }
                    }
                } catch (...) {}
            };
        }
        result = performCurlRequestStreaming(url, body.dump(), headers, on_sse);
    } else {
        result = performCurlRequest(url, body.dump(), headers);
    }

    try {
        nlohmann::json json_result = nlohmann::json::parse(result);

        if (stream && json_result.contains("sse_events")) {
            return parseStreamingEvents(
                json_result["sse_events"].get<std::vector<std::string>>(),
                provider_type
            );
        }

        if (json_result.contains("error")) {
            response.error_message = json_result["error"].is_string()
                ? json_result["error"].get<std::string>()
                : json_result["error"].dump();
            response.error_category = errorCategoryFromName(
                json_result.value("error_category", "provider_error")
            );
            return response;
        }

        // Non-streaming response parsing
        if (json_result.contains("choices") && !json_result["choices"].empty()) {
            auto& choice = json_result["choices"][0];
            if (choice.contains("message")) {
                auto& message = choice["message"];
                if (message.contains("content") && !message["content"].is_null()) {
                    response.content = message["content"].get<std::string>();
                }
                if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
                    for (const auto& tc : message["tool_calls"]) {
                        ToolCall call;
                        call.id = tc.value("id", "");
                        call.type = tc.value("type", "function");
                        if (tc.contains("function")) {
                            call.name = tc["function"].value("name", "");
                            std::string args_str =
                                tc["function"].value("arguments", "{}");
                            try {
                                call.arguments = nlohmann::json::parse(args_str);
                            } catch (const std::exception& e) {
                                call.arguments_error =
                                    std::string("Malformed tool arguments: ")
                                    + e.what();
                            }
                        }
                        response.tool_calls.push_back(call);
                    }
                }
            }
            if (json_result.contains("usage")) {
                response.input_tokens =
                    json_result["usage"].value("prompt_tokens", int64_t{0});
                response.output_tokens =
                    json_result["usage"].value("completion_tokens", int64_t{0});
            }
        } else if (json_result.contains("content") && json_result["content"].is_array()) {
            response.content_blocks = json_result["content"];
            for (const auto& block : json_result["content"]) {
                std::string type = block.value("type", "");
                if (type == "text" && block.contains("text") && block["text"].is_string()) {
                    if (!response.content.empty()) response.content += "\n";
                    response.content += block["text"].get<std::string>();
                } else if (type == "tool_use") {
                    ToolCall call;
                    call.id = block.value("id", "");
                    call.name = block.value("name", "");
                    call.input = block.contains("input")
                        ? block["input"]
                        : nlohmann::json::object();
                    response.tool_calls.push_back(call);
                }
            }
            if (json_result.contains("usage")) {
                response.input_tokens =
                    json_result["usage"].value("input_tokens", int64_t{0});
                response.output_tokens =
                    json_result["usage"].value("output_tokens", int64_t{0});
            }
        }

        response.has_tool_calls = !response.tool_calls.empty();
        response.success =
            !response.content.empty() || !response.tool_calls.empty();

    } catch (const std::exception& e) {
        response.error_message =
            "JSON parse error: " + std::string(e.what());
        response.content = result;
    }

    return response;
}

bool HttpClient::validateApiKey(
    const std::string& url,
    const std::string& api_key,
    const std::string& model
) {
    std::vector<ChatMessage> test_messages = {
        {"user", "Hello", nullptr, "", {}}
    };
    ChatResponse response = sendChatRequest(
        url, api_key, model, test_messages, "OpenAICompatible"
    );
    return response.success;
}

} // namespace opencode
