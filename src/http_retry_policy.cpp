#include "http_retry_policy.hpp"

#include <algorithm>
#include <cctype>

namespace opencode {
namespace {

bool containsInsensitive(std::string text, std::string needle) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return text.find(needle) != std::string::npos;
}

bool transientCurl(CURLcode code) {
    switch (code) {
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_CONNECT:
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_PARTIAL_FILE:
        case CURLE_HTTP2:
            return true;
        default:
            return false;
    }
}

} // namespace

const char* errorCategoryName(ErrorCategory category) {
    switch (category) {
        case ErrorCategory::None: return "none";
        case ErrorCategory::Authentication: return "authentication";
        case ErrorCategory::RateLimited: return "rate_limited";
        case ErrorCategory::ContextOverflow: return "context_overflow";
        case ErrorCategory::Transport: return "transport";
        case ErrorCategory::Provider: return "provider_error";
        case ErrorCategory::InvalidToolCall: return "invalid_tool_call";
        case ErrorCategory::ToolExecution: return "tool_execution";
        case ErrorCategory::Cancelled: return "cancelled";
    }
    return "provider_error";
}

ErrorCategory errorCategoryFromName(const std::string& name) {
    if (name == "authentication") return ErrorCategory::Authentication;
    if (name == "rate_limited") return ErrorCategory::RateLimited;
    if (name == "context_overflow") return ErrorCategory::ContextOverflow;
    if (name == "transport") return ErrorCategory::Transport;
    if (name == "invalid_tool_call") return ErrorCategory::InvalidToolCall;
    if (name == "tool_execution") return ErrorCategory::ToolExecution;
    if (name == "cancelled") return ErrorCategory::Cancelled;
    if (name == "none") return ErrorCategory::None;
    return ErrorCategory::Provider;
}

RetryDecision HttpRetryPolicy::assess(
    long status,
    CURLcode curl_code,
    const std::string& body,
    std::size_t completed_retries,
    long retry_after_ms
) {
    RetryDecision result;
    if (curl_code != CURLE_OK) {
        result.category = curl_code == CURLE_ABORTED_BY_CALLBACK
            ? ErrorCategory::Cancelled
            : ErrorCategory::Transport;
        result.retry = transientCurl(curl_code);
    } else if (status == 401 || status == 403) {
        result.category = ErrorCategory::Authentication;
    } else if (status == 429) {
        result.category = ErrorCategory::RateLimited;
        result.retry = true;
    } else if (status == 408 || status == 409 || status == 425 ||
               status == 500 || status == 502 || status == 503 ||
               status == 504) {
        result.category = ErrorCategory::Provider;
        result.retry = true;
    } else if (status >= 400) {
        result.category =
            (status == 400 &&
             (containsInsensitive(body, "context") ||
              containsInsensitive(body, "maximum token")))
                ? ErrorCategory::ContextOverflow
                : ErrorCategory::Provider;
    }
    if (completed_retries >= kMaxRetries) result.retry = false;
    if (result.retry) {
        const long exponential =
            500L * (1L << std::min<std::size_t>(completed_retries, 4));
        // Small deterministic jitter prevents synchronized clients while
        // keeping behavior reproducible in tests.
        const long jitter = static_cast<long>((completed_retries * 137 + 53) % 211);
        result.delay_ms = std::min<long>(
            30000L,
            retry_after_ms >= 0 ? retry_after_ms : exponential + jitter
        );
    }
    return result;
}

} // namespace opencode
