#ifndef MODEL_PROVIDER_HPP
#define MODEL_PROVIDER_HPP

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#include "http_client.hpp"

namespace opencode {

struct ModelCapabilities {
    std::size_t context_window = 128000;
    std::size_t max_output_tokens = 4096;
    bool native_tool_calls = true;
    bool streaming_usage = true;
    bool image_input = false;
};

class ModelProvider {
public:
    virtual ~ModelProvider() = default;
    virtual std::string name() const = 0;
    virtual std::string model() const = 0;
    virtual ModelCapabilities capabilities() const = 0;
    virtual ChatResponse request(
        const std::vector<ChatMessage>& messages,
        const std::vector<nlohmann::json>& tools,
        const std::function<void(const std::string&)>& on_delta
    ) = 0;
    virtual void appendToolResults(
        std::vector<ChatMessage>& messages,
        const std::vector<ToolCall>& calls,
        const std::vector<nlohmann::json>& results
    ) const = 0;
};

/// HTTP-based ModelProvider. Subclasses define provider-specific request
/// encoding, response parsing, stream delta extraction, and tool-result
/// formatting.  HttpClient is used for raw transport only — no provider-name
/// conditionals remain in the HTTP layer.
class HttpModelProvider : public ModelProvider {
public:
    HttpModelProvider(
        std::string provider_name,
        HttpClient& client,
        std::string endpoint,
        std::string api_key,
        std::string model,
        ModelCapabilities capabilities
    );

    std::string name() const override;
    std::string model() const override;
    ModelCapabilities capabilities() const override;

    ChatResponse request(
        const std::vector<ChatMessage>& messages,
        const std::vector<nlohmann::json>& tools,
        const std::function<void(const std::string&)>& on_delta
    ) override;

    void appendToolResults(
        std::vector<ChatMessage>& messages,
        const std::vector<ToolCall>& calls,
        const std::vector<nlohmann::json>& results
    ) const override;

protected:
    /// Build the provider-specific JSON request body.
    virtual nlohmann::json buildRequestBody(
        const std::vector<ChatMessage>& messages,
        const std::vector<nlohmann::json>& tools,
        bool stream
    ) const = 0;

    /// Extract visible text from one SSE event (provider-specific format).
    virtual std::string extractTextDelta(
        const std::string& sse_event
    ) const = 0;

    /// Parse a complete set of streaming SSE events into a ChatResponse.
    virtual ChatResponse parseStreamingResponse(
        const std::vector<std::string>& sse_events
    ) const = 0;

    /// Parse a non-streaming JSON response body into a ChatResponse.
    virtual ChatResponse parseNonStreamingResponse(
        const std::string& response_body
    ) const = 0;

    /// HTTP headers required for every API call (auth, version, etc.).
    virtual std::vector<HttpClient::Header> requestHeaders() const = 0;

    /// Provider-specific tool-result message formatting.
    virtual void appendToolResultsImpl(
        std::vector<ChatMessage>& messages,
        const std::vector<ToolCall>& calls,
        const std::vector<nlohmann::json>& results
    ) const = 0;

    HttpClient& httpClient() { return client_; }
    const std::string& endpoint() const { return endpoint_; }
    const std::string& apiKey() const { return api_key_; }

private:
    std::string provider_name_;
    HttpClient& client_;
    std::string endpoint_;
    std::string api_key_;
    std::string model_;
    ModelCapabilities capabilities_;
};

std::unique_ptr<ModelProvider> createModelProvider(
    const std::string& provider,
    HttpClient& client,
    std::string endpoint,
    std::string api_key,
    std::string model
);

} // namespace opencode

#endif
