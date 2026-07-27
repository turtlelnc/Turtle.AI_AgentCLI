#ifndef ENDPOINT_POLICY_HPP
#define ENDPOINT_POLICY_HPP

#include <string>

namespace opencode {

struct EndpointDecision {
    bool allowed = false;
    bool credential_confirmation_required = false;
    std::string host;
    std::string error;
};

class EndpointPolicy {
public:
    static constexpr bool allowHttpRedirects() { return false; }
    static EndpointDecision assess(
        const std::string& provider,
        const std::string& url,
        bool has_credentials
    );
};

} // namespace opencode

#endif
