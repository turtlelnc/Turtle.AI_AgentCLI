#ifndef CHANGE_JOURNAL_HPP
#define CHANGE_JOURNAL_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace opencode {

class ChangeJournal {
public:
    struct Snapshot {
        std::filesystem::path target;
        bool existed = false;
        bool directory = false;
        std::vector<std::pair<std::filesystem::path, std::string>> files;
    };

    /// Capture a snapshot of target file(s). For large files (>1 MiB), only
    /// the path and SHA-256 hash are stored instead of full content.
    Snapshot capture(const std::filesystem::path& target) const;

    /// Record a change with thread/turn/tool context.
    std::uint64_t record(
        const std::string& action,
        const std::filesystem::path& target,
        Snapshot before,
        const std::string& thread_id = "",
        const std::string& turn_id = "",
        const std::string& tool_call_id = ""
    );

    /// Compute SHA-256 hash of a file. Returns empty string on error.
    static std::string fileHash(const std::filesystem::path& path);

    /// Check whether a file has been modified externally since the snapshot.
    bool hasExternalModification(std::uint64_t id) const;

    nlohmann::json list() const;
    nlohmann::json undo(std::uint64_t id = 0);

private:
    static constexpr std::uintmax_t kMaxContentSize = 1 * 1024 * 1024; // 1 MiB
    static constexpr std::size_t kMaxEntries = 1000;
    static constexpr std::size_t kMaxTotalFiles = 5000;

    struct Entry {
        std::uint64_t id;
        std::string action;
        std::filesystem::path target;
        Snapshot before;
        std::string before_hash;   // SHA-256 when content was captured as hash
        std::string thread_id;
        std::string turn_id;
        std::string tool_call_id;
        bool undone = false;
    };

    std::vector<Entry> entries_;
    std::uint64_t next_id_ = 1;
    std::size_t total_files_ = 0;
};

} // namespace opencode

#endif
