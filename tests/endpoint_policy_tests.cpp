#include "endpoint_policy.hpp"

#include <iostream>
#include <string>

namespace {
bool expect(bool condition, const std::string& message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main() {
    using opencode::EndpointPolicy;
    bool passed = expect(
        !EndpointPolicy::allowHttpRedirects(),
        "credential-bearing HTTP redirects are disabled"
    );
    passed &= expect(
        EndpointPolicy::assess(
            "OpenAI",
            "https://api.openai.com/v1/chat/completions",
            true
        ).allowed,
        "official OpenAI endpoint is allowed"
    );
    passed &= expect(
        !EndpointPolicy::assess(
            "OpenAI", "https://evil.example/v1/chat/completions", true
        ).allowed,
        "official credentials cannot target another host"
    );
    passed &= expect(
        !EndpointPolicy::assess(
            "Anthropic", "http://api.anthropic.com/v1/messages", true
        ).allowed,
        "remote HTTP is rejected"
    );
    passed &= expect(
        EndpointPolicy::assess(
            "LlamaCpp", "http://localhost:8080/v1/chat/completions", false
        ).allowed &&
        EndpointPolicy::assess(
            "LlamaCpp", "http://127.0.0.1:11434/v1/chat/completions", false
        ).allowed &&
        EndpointPolicy::assess(
            "LlamaCpp", "http://[::1]:8080/v1/chat/completions", false
        ).allowed,
        "loopback HTTP remains available"
    );
    passed &= expect(
        !EndpointPolicy::assess(
            "OpenAICompatible",
            "http://192.168.1.10:8080/v1/chat/completions",
            false
        ).allowed,
        "non-loopback HTTP is rejected"
    );
    passed &= expect(
        !EndpointPolicy::assess(
            "OpenAICompatible",
            "http://127.0.0.1.evil.example/v1/chat/completions",
            true
        ).allowed,
        "loopback-looking DNS names cannot bypass HTTPS"
    );
    const auto custom = EndpointPolicy::assess(
        "OpenAICompatible",
        "https://gateway.example/v1/chat/completions",
        true
    );
    passed &= expect(
        custom.allowed && custom.credential_confirmation_required &&
            custom.host == "gateway.example",
        "custom HTTPS credential target requires confirmation"
    );
    passed &= expect(
        !EndpointPolicy::assess(
            "OpenAICompatible",
            "https://user:pass@gateway.example/v1/chat/completions",
            false
        ).allowed,
        "URL user information is rejected"
    );
    return passed ? 0 : 1;
}
