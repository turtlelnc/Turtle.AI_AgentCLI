#ifndef HTTP_RETRY_POLICY_HPP
#define HTTP_RETRY_POLICY_HPP

#include <cstddef>
#include <string>

#include <curl/curl.h>
#include "error_category.hpp"

namespace opencode {

struct RetryDecision {
    ErrorCategory category = ErrorCategory::None;
    bool retry = false;
    long delay_ms = 0;
};

class HttpRetryPolicy {
public:
    static constexpr std::size_t kMaxRetries = 3;

    static RetryDecision assess(
        long http_status,
        CURLcode curl_code,
        const std::string& response_body,
        std::size_t completed_retries,
        long retry_after_ms = -1
    );
};

} // namespace opencode

#endif
