#ifndef SESSION_MANAGER_HPP
#define SESSION_MANAGER_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "http_client.hpp"
#include "session_model.hpp"

namespace opencode {

struct SessionInfo {
    std::string id;
    std::string title;
    std::string provider;
    std::string model;
    std::int64_t updated_at = 0;
    std::size_t message_count = 0;
    std::size_t turn_count = 0;
};

class SessionManager {
public:
    bool setWorkspace(const std::filesystem::path& workspace);

    std::string create(
        const std::string& provider,
        const std::string& model
    );

    // ─── Legacy (ChatMessage-based) ────────────────────────────────────
    bool save(
        const std::string& id,
        const std::string& provider,
        const std::string& model,
        const std::vector<ChatMessage>& messages,
        std::string* error = nullptr
    ) const;
    bool load(
        const std::string& id,
        std::vector<ChatMessage>& messages,
        SessionInfo& info,
        std::string* error = nullptr
    ) const;

    // ─── Structured (Thread/Turn-based) ────────────────────────────────
    bool saveThread(const Thread& thread, std::string* error = nullptr) const;
    std::optional<Thread> loadThread(
        const std::string& id, std::string* error = nullptr
    ) const;

    /// Fork an existing thread into a new thread id.
    std::optional<Thread> forkThread(
        const std::string& source_id,
        const std::string& new_id,
        std::string* error = nullptr
    ) const;

    std::vector<SessionInfo> list() const;

private:
    std::filesystem::path sessions_root_;
    static bool validId(const std::string& id);
};

} // namespace opencode

#endif
