#include "http_retry_policy.hpp"

#include <iostream>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
}

int main() {
    using opencode::ErrorCategory;
    using opencode::HttpRetryPolicy;
    bool ok = true;
    auto auth = HttpRetryPolicy::assess(401, CURLE_OK, "", 0);
    ok &= expect(auth.category == ErrorCategory::Authentication && !auth.retry,
                 "authentication errors are not retried");
    auto limited = HttpRetryPolicy::assess(429, CURLE_OK, "", 0, 4200);
    ok &= expect(limited.category == ErrorCategory::RateLimited &&
                     limited.retry && limited.delay_ms == 4200,
                 "429 respects Retry-After");
    auto server = HttpRetryPolicy::assess(503, CURLE_OK, "", 1);
    ok &= expect(server.retry && server.delay_ms >= 1000,
                 "503 gets exponential backoff");
    auto exhausted = HttpRetryPolicy::assess(
        503, CURLE_OK, "", HttpRetryPolicy::kMaxRetries
    );
    ok &= expect(!exhausted.retry, "retry count is bounded");
    auto context = HttpRetryPolicy::assess(
        400, CURLE_OK, "maximum context length exceeded", 0
    );
    ok &= expect(context.category == ErrorCategory::ContextOverflow &&
                     !context.retry,
                 "context overflow is classified without blind retry");
    auto network = HttpRetryPolicy::assess(
        0, CURLE_OPERATION_TIMEDOUT, "", 0
    );
    ok &= expect(network.category == ErrorCategory::Transport && network.retry,
                 "transient transport failure is retried");
    auto cancelled = HttpRetryPolicy::assess(
        0, CURLE_ABORTED_BY_CALLBACK, "", 0
    );
    ok &= expect(cancelled.category == ErrorCategory::Cancelled &&
                     !cancelled.retry,
                 "cancelled requests are never retried");
    return ok ? 0 : 1;
}
