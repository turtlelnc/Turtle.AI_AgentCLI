#ifndef CONTEXT_MANAGER_HPP
#define CONTEXT_MANAGER_HPP

#include <cstddef>
#include <vector>

#include "http_client.hpp"
#include "model_provider.hpp"

namespace opencode {

struct ContextStats {
    std::size_t original_messages = 0;
    std::size_t visible_messages = 0;
    std::size_t omitted_messages = 0;
    std::size_t estimated_tokens = 0;
    bool compressed = false;
};

struct ContextWindow {
    std::vector<ChatMessage> messages;
    ContextStats stats;
};

class ContextManager {
public:
    ContextWindow build(
        const std::vector<ChatMessage>& full_history,
        const ModelCapabilities& capabilities
    );

    static std::size_t estimateTokens(const std::vector<ChatMessage>& messages);

    /// Returns true when the system message content has changed since the
    /// previous build.  Useful for deciding whether to clear caches or
    /// re-inject large invariant instructions.
    bool systemPromptChanged() const { return system_prompt_changed_; }

private:
    static constexpr std::size_t kTokenReserve = 1024;
    static constexpr std::size_t kMaxToolResultBytes = 32768;

    // Track the system-prompt fingerprint across builds so we can avoid
    // re-injecting unchanged large content (and prepare for prompt caching).
    std::size_t last_system_hash_ = 0;
    bool system_prompt_changed_ = true; // first build always counts as changed
};

} // namespace opencode

#endif
