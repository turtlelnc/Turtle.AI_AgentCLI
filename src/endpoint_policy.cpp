#include "endpoint_policy.hpp"

#include <algorithm>
#include <cctype>

namespace opencode {

namespace {

struct ParsedUrl {
    std::string scheme;
    std::string host;
    std::string port;
    std::string path;
    std::string error;
};

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool digitsOnly(const std::string& value) {
    return !value.empty() &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return std::isdigit(c);
        });
}

bool validHostCharacters(const std::string& host) {
    return std::all_of(host.begin(), host.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '-' || c == ':';
    });
}

ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl result;
    if (url.find('#') != std::string::npos) {
        result.error = "URL fragments are not allowed";
        return result;
    }
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        result.error = "Endpoint must include a URL scheme";
        return result;
    }
    result.scheme = lower(url.substr(0, scheme_end));
    const auto authority_start = scheme_end + 3;
    const auto authority_end = url.find_first_of("/?", authority_start);
    const std::string authority = url.substr(
        authority_start,
        authority_end == std::string::npos
            ? std::string::npos : authority_end - authority_start
    );
    if (authority.empty() || authority.find('@') != std::string::npos) {
        result.error = "Endpoint host is missing or contains user information";
        return result;
    }
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string::npos) {
            result.error = "Invalid bracketed IPv6 host";
            return result;
        }
        result.host = lower(authority.substr(1, close - 1));
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') {
                result.error = "Invalid IPv6 endpoint authority";
                return result;
            }
            result.port = authority.substr(close + 2);
        }
    } else {
        const auto colon = authority.rfind(':');
        if (colon != std::string::npos) {
            if (authority.find(':') != colon) {
                result.error = "IPv6 hosts must use brackets";
                return result;
            }
            result.host = lower(authority.substr(0, colon));
            result.port = authority.substr(colon + 1);
        } else {
            result.host = lower(authority);
        }
    }
    if (result.host.empty() || !validHostCharacters(result.host) ||
        (!result.port.empty() && !digitsOnly(result.port))) {
        result.error = "Invalid endpoint host or port";
        return result;
    }
    if (!result.port.empty()) {
        try {
            const int port = std::stoi(result.port);
            if (port < 1 || port > 65535) {
                result.error = "Endpoint port is outside 1-65535";
                return result;
            }
        } catch (...) {
            result.error = "Invalid endpoint port";
            return result;
        }
    }
    result.path = authority_end == std::string::npos
        ? "/" : url.substr(authority_end);
    return result;
}

bool isLoopback(const std::string& host) {
    if (host == "localhost" || host == "::1") return true;
    if (host.rfind("127.", 0) != 0) return false;
    std::size_t start = 0;
    int parts = 0;
    while (start < host.size()) {
        const auto end = host.find('.', start);
        const std::string part = host.substr(
            start, end == std::string::npos ? std::string::npos : end - start
        );
        if (!digitsOnly(part)) return false;
        try {
            if (std::stoi(part) > 255) return false;
        } catch (...) {
            return false;
        }
        ++parts;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return parts == 4;
}

EndpointDecision deny(const ParsedUrl& url, const std::string& message) {
    return {false, false, url.host, message};
}

} // namespace

EndpointDecision EndpointPolicy::assess(
    const std::string& provider,
    const std::string& url,
    bool has_credentials
) {
    const ParsedUrl parsed = parseUrl(url);
    if (!parsed.error.empty()) return deny(parsed, parsed.error);
    if (parsed.scheme != "https" &&
        !(parsed.scheme == "http" && isLoopback(parsed.host))) {
        return deny(
            parsed,
            "Remote endpoints require HTTPS; HTTP is allowed only for loopback hosts"
        );
    }

    const std::string normalized = lower(provider);
    std::string official_host;
    std::string official_path;
    if (normalized == "openai") {
        official_host = "api.openai.com";
        official_path = "/v1/chat/completions";
    } else if (normalized == "anthropic") {
        official_host = "api.anthropic.com";
        official_path = "/v1/messages";
    } else if (normalized == "deepseek") {
        official_host = "api.deepseek.com";
        official_path = "/v1/chat/completions";
    }

    if (!official_host.empty()) {
        const bool default_port =
            parsed.port.empty() || parsed.port == "443";
        if (parsed.scheme != "https" || parsed.host != official_host ||
            !default_port || parsed.path != official_path) {
            return deny(
                parsed,
                "Official " + provider +
                    " credentials may only be sent to https://" +
                    official_host + official_path
            );
        }
        return {true, false, parsed.host, ""};
    }

    if (normalized != "llamacpp" &&
        normalized != "openaicompatible") {
        return deny(parsed, "Unknown provider endpoint policy");
    }
    return {
        true,
        has_credentials,
        parsed.host,
        ""
    };
}

} // namespace opencode
