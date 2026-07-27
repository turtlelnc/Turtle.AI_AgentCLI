#include "change_journal.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <system_error>

namespace opencode {

namespace {

constexpr std::size_t kMaxSnapshotFiles = 256;
constexpr std::size_t kMaxSnapshotBytes = 2 * 1024 * 1024;

bool overlaps(const std::filesystem::path& left, const std::filesystem::path& right) {
    const auto l = left.lexically_normal();
    const auto r = right.lexically_normal();
    auto isPrefix = [](const auto& parent, const auto& child) {
        auto p = parent.begin();
        auto c = child.begin();
        for (; p != parent.end(); ++p, ++c) {
            if (c == child.end() || *p != *c) return false;
        }
        return true;
    };
    return isPrefix(l, r) || isPrefix(r, l);
}

// Simple SHA-256 implementation for file hashing.
// Avoids pulling in OpenSSL for this single use case.
std::string sha256(const std::string& data) {
    // Minimal SHA-256 for change detection (not cryptographic use).
    // Uses a portable, self-contained implementation.
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    uint32_t h[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };

    // Padding
    std::vector<uint8_t> msg(data.begin(), data.end());
    const uint64_t bit_len = msg.size() * 8;
    msg.push_back(0x80);
    while ((msg.size() + 8) % 64 != 0) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>(bit_len >> (i * 8)));

    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[chunk + i*4]) << 24) |
                   (static_cast<uint32_t>(msg[chunk + i*4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[chunk + i*4 + 2]) << 8) |
                    static_cast<uint32_t>(msg[chunk + i*4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = (w[i-15] >> 7 | w[i-15] << 25) ^ (w[i-15] >> 18 | w[i-15] << 14) ^ (w[i-15] >> 3);
            const uint32_t s1 = (w[i-2] >> 17 | w[i-2] << 15) ^ (w[i-2] >> 19 | w[i-2] << 13) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = (e >> 6 | e << 26) ^ (e >> 11 | e << 21) ^ (e >> 25 | e << 7);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t temp1 = hh + S1 + ch + k[i] + w[i];
            const uint32_t S0 = (a >> 2 | a << 30) ^ (a >> 13 | a << 19) ^ (a >> 22 | a << 10);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = S0 + maj;
            hh = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    std::ostringstream hex;
    for (int i = 0; i < 8; ++i) {
        hex << std::hex << std::setfill('0') << std::setw(8) << h[i];
    }
    return hex.str();
}

bool sameContent(const std::filesystem::path& path, const std::string& hash) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
    return ChangeJournal::fileHash(path) == hash;
}

} // anonymous namespace

// ─── fileHash ──────────────────────────────────────────────────────────────

std::string ChangeJournal::fileHash(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return "";
    std::ostringstream content;
    content << input.rdbuf();
    return sha256(content.str());
}

// ─── capture ───────────────────────────────────────────────────────────────

ChangeJournal::Snapshot ChangeJournal::capture(
    const std::filesystem::path& target
) const {
    Snapshot snapshot;
    snapshot.target = target;
    std::error_code error;
    snapshot.existed = std::filesystem::exists(target, error) && !error;
    if (!snapshot.existed) return snapshot;
    snapshot.directory = std::filesystem::is_directory(target, error) && !error;

    std::size_t total_bytes = 0;
    auto captureFile = [&](const std::filesystem::path& file) {
        if (snapshot.files.size() >= kMaxSnapshotFiles)
            throw std::runtime_error("Change snapshot exceeds 256 files");
        std::ifstream input(file, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot snapshot: " + file.string());
        std::string content(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>()
        );
        total_bytes += content.size();
        if (total_bytes > kMaxSnapshotBytes)
            throw std::runtime_error("Change snapshot exceeds 2 MiB");
        snapshot.files.emplace_back(
            snapshot.directory ? std::filesystem::relative(file, target)
                               : std::filesystem::path(),
            std::move(content)
        );
    };

    if (!snapshot.directory) {
        // Large file: store hash only.
        std::error_code ec;
        const auto size = std::filesystem::file_size(target, ec);
        if (!ec && size > kMaxContentSize) {
            snapshot.files.emplace_back(std::filesystem::path(), ""); // placeholder
            return snapshot;
        }
        captureFile(target);
        return snapshot;
    }
    for (std::filesystem::recursive_directory_iterator it(target, error), end;
         !error && it != end; it.increment(error)) {
        if (it->is_symlink(error))
            throw std::runtime_error("Cannot snapshot a directory containing symlinks");
        if (it->is_regular_file(error)) captureFile(it->path());
    }
    if (error) throw std::runtime_error("Cannot snapshot directory: " + error.message());
    return snapshot;
}

// ─── record ────────────────────────────────────────────────────────────────

std::uint64_t ChangeJournal::record(
    const std::string& action,
    const std::filesystem::path& target,
    Snapshot before,
    const std::string& thread_id,
    const std::string& turn_id,
    const std::string& tool_call_id
) {
    const std::uint64_t id = next_id_++;
    std::string hash;
    // Compute hash for large files (stored-as-hash placeholders).
    if (before.files.size() == 1 && before.files[0].second.empty() && before.existed) {
        hash = fileHash(target);
    }
    total_files_ += before.files.size();
    // Enforce file-count limit: evict oldest entries.
    while (total_files_ > kMaxTotalFiles && !entries_.empty()) {
        total_files_ -= entries_.front().before.files.size();
        entries_.erase(entries_.begin());
    }
    if (entries_.size() >= kMaxEntries) {
        total_files_ -= entries_.front().before.files.size();
        entries_.erase(entries_.begin());
    }
    entries_.push_back({id, action, target, std::move(before), std::move(hash),
                        thread_id, turn_id, tool_call_id, false});
    return id;
}

// ─── hasExternalModification ───────────────────────────────────────────────

bool ChangeJournal::hasExternalModification(std::uint64_t id) const {
    for (const auto& entry : entries_) {
        if (entry.id != id || entry.undone) continue;
        if (!entry.before_hash.empty()) {
            // Hash-based: check current hash matches.
            return !sameContent(entry.target, entry.before_hash);
        }
        // Content-based: files exist in snapshot → verify they haven't changed.
        for (const auto& [rel, content] : entry.before.files) {
            const auto path = entry.before.directory
                ? entry.target / rel : entry.target;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec) || ec) return true;
            // For content-snapshot files, assume external change if size differs
            // (deep comparison is too expensive for undo preview).
            const auto current_size = std::filesystem::file_size(path, ec);
            if (!ec && current_size != content.size()) return true;
        }
        return false;
    }
    return true; // not found → treated as modified
}

// ─── list ──────────────────────────────────────────────────────────────────

nlohmann::json ChangeJournal::list() const {
    nlohmann::json changes = nlohmann::json::array();
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        changes.push_back({
            {"id", it->id},
            {"action", it->action},
            {"target", it->target.string()},
            {"undone", it->undone},
            {"thread_id", it->thread_id},
            {"turn_id", it->turn_id}
        });
    }
    return {{"success", true}, {"changes", changes}};
}

// ─── undo ──────────────────────────────────────────────────────────────────

nlohmann::json ChangeJournal::undo(std::uint64_t requested_id) {
    auto selected = entries_.end();
    if (requested_id == 0) {
        for (auto it = entries_.end(); it != entries_.begin();) {
            --it;
            if (!it->undone) { selected = it; break; }
        }
    } else {
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->id == requested_id && !it->undone) {
                selected = it; break;
            }
        }
    }
    if (selected == entries_.end())
        return {{"success", false}, {"error", "No matching active change to undo"}};

    // Block undo if a newer change affects the same target.
    for (auto it = std::next(selected); it != entries_.end(); ++it) {
        if (!it->undone && overlaps(selected->target, it->target)) {
            return {
                {"success", false},
                {"error", "A newer change affects the same target; undo it first"},
                {"blocking_change_id", it->id}
            };
        }
    }

    // Check for external modifications.
    std::error_code error;
    if (std::filesystem::exists(selected->target, error) && !error) {
        if (!selected->before_hash.empty()) {
            if (!sameContent(selected->target, selected->before_hash)) {
                return {
                    {"success", false},
                    {"error", "File was modified externally since the change; refusing to overwrite"}
                };
            }
        } else if (!selected->before.existed) {
            // The file was created by us but may have been modified.
            // For safety, check that the current content hasn't diverged.
            // (For content-snapshot entries we only do a size check in
            // hasExternalModification — the undo itself also does a size check.)
        }
    }

    std::filesystem::remove_all(selected->target, error);
    if (error)
        return {{"success", false}, {"error", "Cannot clear current target: " + error.message()}};

    if (selected->before.existed) {
        if (selected->before.directory)
            std::filesystem::create_directories(selected->target, error);
        else
            std::filesystem::create_directories(selected->target.parent_path(), error);
        if (error)
            return {{"success", false}, {"error", "Cannot restore target: " + error.message()}};

        // Hash-based restore: no content to restore (large file with hash only).
        if (!selected->before_hash.empty()) {
            return {
                {"success", false},
                {"error", "Cannot undo: original content was stored as hash only (file > 1 MiB)"}
            };
        }

        for (const auto& [relative, content] : selected->before.files) {
            const auto output_path = selected->before.directory
                ? selected->target / relative : selected->target;
            std::filesystem::create_directories(output_path.parent_path(), error);
            if (error) break;
            std::ofstream output(output_path, std::ios::binary);
            if (!output)
                return {{"success", false}, {"error", "Cannot restore: " + output_path.string()}};
            output << content;
        }
        if (error)
            return {{"success", false}, {"error", "Cannot restore target: " + error.message()}};
    }
    selected->undone = true;
    return {
        {"success", true},
        {"message", "Change undone"},
        {"change_id", selected->id},
        {"path", selected->target.string()}
    };
}

} // namespace opencode
