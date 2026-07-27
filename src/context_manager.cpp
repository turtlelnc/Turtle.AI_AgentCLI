#include "context_manager.hpp"

#include <algorithm>
#include <functional>
#include <set>
#include <sstream>
#include <string>

namespace opencode {
namespace {

constexpr std::size_t kMessageOverheadBytes = 24;

std::size_t messageBytes(const ChatMessage& message) {
    std::size_t size = kMessageOverheadBytes + message.role.size() +
                       message.content.size() + message.tool_call_id.size();
    if (!message.content_blocks.is_null()) size += message.content_blocks.dump().size();
    for (const auto& call : message.tool_calls) {
        size += call.id.size() + call.name.size() + call.arguments.dump().size() +
                call.input.dump().size();
    }
    return size;
}

void truncateText(std::string& text, std::size_t limit, const std::string& marker) {
    if (text.size() <= limit) return;
    if (limit <= marker.size()) {
        text = marker.substr(0, limit);
        return;
    }
    text.resize(limit - marker.size());
    text += marker;
}

ChatMessage boundedCopy(const ChatMessage& source, std::size_t content_limit) {
    ChatMessage copy = source;
    if (copy.role == "tool") {
        // Tool results get a specific marker and a tighter bound.
        truncateText(copy.content, std::min(content_limit, std::size_t{32768}),
                     "\n[tool result truncated]");
    } else {
        truncateText(copy.content, content_limit, "\n[context truncated]");
    }
    return copy;
}

std::string summaryFor(
    const std::vector<ChatMessage>& history,
    const std::set<std::size_t>& selected,
    std::size_t limit
) {
    std::ostringstream out;
    out << "# Earlier conversation summary\n";
    for (std::size_t i = 0; i < history.size(); ++i) {
        if (selected.count(i)) continue;
        std::string snippet = history[i].content;
        std::replace(snippet.begin(), snippet.end(), '\n', ' ');
        truncateText(snippet, 160, "...");
        out << history[i].role << ": " << snippet << '\n';
        if (out.str().size() >= limit) break;
    }
    std::string result = out.str();
    truncateText(result, limit, "\n[summary truncated]");
    return result;
}

bool isSystem(const ChatMessage& message) {
    return message.role == "system";
}

bool isConversationSummary(const ChatMessage& message) {
    return message.role == "system" &&
           message.content.rfind("# Earlier conversation summary", 0) == 0;
}

} // namespace

std::size_t ContextManager::estimateTokens(
    const std::vector<ChatMessage>& messages
) {
    std::size_t bytes = 0;
    for (const auto& message : messages) bytes += messageBytes(message);
    return (bytes + 3) / 4;
}

ContextWindow ContextManager::build(
    const std::vector<ChatMessage>& full_history,
    const ModelCapabilities& capabilities
) {
    ContextWindow result;
    result.stats.original_messages = full_history.size();
    if (full_history.empty()) return result;

    // Track system-prompt fingerprint so callers can detect when the
    // large invariant instructions actually changed between turns.
    std::size_t system_index = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < full_history.size(); ++i) {
        if (isSystem(full_history[i])) {
            system_index = i;
            break;
        }
    }
    if (system_index != static_cast<std::size_t>(-1)) {
        const std::size_t current_hash =
            std::hash<std::string>{}(full_history[system_index].content);
        system_prompt_changed_ = (current_hash != last_system_hash_);
        last_system_hash_ = current_hash;
    } else {
        system_prompt_changed_ = true;
        last_system_hash_ = 0;
    }

    const std::size_t usable_tokens =
        capabilities.context_window > capabilities.max_output_tokens + kTokenReserve
            ? capabilities.context_window - capabilities.max_output_tokens - kTokenReserve
            : std::max<std::size_t>(64, capabilities.context_window / 2);
    const std::size_t byte_budget = std::max<std::size_t>(256, usable_tokens * 4);

    // Bound individual messages, but never truncate the system prompt — it
    // contains the model's core instructions and must stay intact.
    std::vector<ChatMessage> bounded;
    bounded.reserve(full_history.size());
    for (const auto& message : full_history) {
        if (isSystem(message)) {
            bounded.push_back(message);        // keep intact
        } else {
            bounded.push_back(boundedCopy(
                message,
                message.role == "tool" ? kMaxToolResultBytes : byte_budget / 2
            ));
        }
    }

    if (estimateTokens(bounded) <= usable_tokens) {
        result.messages = std::move(bounded);
        result.stats.visible_messages = result.messages.size();
        result.stats.estimated_tokens = estimateTokens(result.messages);
        return result;
    }

    // Select mandatory messages.
    std::set<std::size_t> selected;
    for (std::size_t i = 0; i < bounded.size(); ++i) {
        if (bounded[i].role == "system") {
            selected.insert(i);
            break;
        }
    }
    for (std::size_t i = bounded.size(); i-- > 0;) {
        if (bounded[i].role == "user") {
            selected.insert(i);
            break;
        }
    }

    // Preserve the active model/tool chain as an indivisible recent suffix.
    std::size_t chain_start = bounded.size();
    for (std::size_t i = bounded.size(); i-- > 0;) {
        if (bounded[i].role == "assistant" && !bounded[i].tool_calls.empty()) {
            chain_start = i;
            break;
        }
        if (bounded[i].role == "user") break;
    }
    if (chain_start < bounded.size()) {
        for (std::size_t i = chain_start; i < bounded.size(); ++i) selected.insert(i);
    }

    const std::size_t summary_reserve = std::min<std::size_t>(2048, byte_budget / 5);
    std::size_t used = 0;
    for (const auto index : selected) used += messageBytes(bounded[index]);
    for (std::size_t i = bounded.size(); i-- > 0;) {
        if (selected.count(i)) continue;
        const std::size_t cost = messageBytes(bounded[i]);
        if (used + cost + summary_reserve <= byte_budget) {
            selected.insert(i);
            used += cost;
        }
    }

    // Mandatory messages may themselves be too large. Bound non-system copies.
    if (used + summary_reserve > byte_budget && !selected.empty()) {
        std::size_t non_system = 0;
        for (const auto index : selected) {
            if (!isSystem(bounded[index])) ++non_system;
        }
        if (non_system > 0) {
            const std::size_t per_message =
                std::max<std::size_t>(48, (byte_budget - summary_reserve) / non_system);
            used = 0;
            for (const auto index : selected) {
                if (!isSystem(bounded[index])) {
                    bounded[index] = boundedCopy(bounded[index], per_message / 2);
                }
                used += messageBytes(bounded[index]);
            }
        }
    }

    const std::size_t omitted = bounded.size() - selected.size();
    if (omitted > 0 && used < byte_budget) {
        ChatMessage summary;
        summary.role = "system";
        summary.content = summaryFor(
            full_history, selected,
            std::min(summary_reserve, byte_budget - used)
        );
        if (!summary.content.empty()) result.messages.push_back(std::move(summary));
    }
    for (std::size_t i = 0; i < bounded.size(); ++i) {
        if (selected.count(i)) result.messages.push_back(std::move(bounded[i]));
    }

    // Hard ceiling: trim synthetic summaries and non-system messages first;
    // the system prompt is the last thing we shrink.
    while (estimateTokens(result.messages) > usable_tokens &&
           !result.messages.empty()) {
        // Find the best trim target: summaries first, then largest non-system.
        auto target = result.messages.end();
        for (auto it = result.messages.begin(); it != result.messages.end(); ++it) {
            if (isConversationSummary(*it)) {
                target = it;
                break;
            }
        }
        if (target == result.messages.end()) {
            // No summary — find the largest trimmable non-system message.
            for (auto it = result.messages.begin(); it != result.messages.end(); ++it) {
                if (isSystem(*it)) continue;       // protect system prompt
                if (target == result.messages.end() ||
                    it->content.size() > target->content.size()) {
                    target = it;
                }
            }
        }
        if (target == result.messages.end()) {
            // Only system messages remain — trim the smallest one.
            for (auto it = result.messages.begin(); it != result.messages.end(); ++it) {
                if (target == result.messages.end() ||
                    it->content.size() < target->content.size()) {
                    target = it;
                }
            }
        }
        if (target == result.messages.end()) break;

        if (target->content.size() > 32) {
            const std::size_t excess =
                estimateTokens(result.messages) * 4 - usable_tokens * 4;
            truncateText(
                target->content,
                std::max<std::size_t>(32, target->content.size() -
                    std::min(excess + 4, target->content.size() - 32)),
                "..."
            );
        } else if (result.messages.size() > selected.size()) {
            // Still over budget and we cannot shrink further — remove a
            // synthetic message (summary) or a non-system message.
            result.messages.erase(target);
        } else {
            break;
        }
    }

    result.stats.visible_messages = result.messages.size();
    result.stats.omitted_messages = omitted;
    result.stats.estimated_tokens = estimateTokens(result.messages);
    result.stats.compressed = true;
    return result;
}

} // namespace opencode
