#ifndef SESSION_MODEL_HPP
#define SESSION_MODEL_HPP

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace opencode {

// ─── Enums ─────────────────────────────────────────────────────────────────

enum class TurnState {
    Running,
    Completed,
    Failed,
    Cancelled
};

inline const char* turnStateName(TurnState s) {
    switch (s) {
        case TurnState::Running:    return "running";
        case TurnState::Completed:  return "completed";
        case TurnState::Failed:     return "failed";
        case TurnState::Cancelled:  return "cancelled";
    }
    return "unknown";
}

// ─── Item types ────────────────────────────────────────────────────────────

struct TokenUsage {
    int64_t input_tokens = 0;
    int64_t output_tokens = 0;
    double cost_usd = 0.0;
};

struct ToolCallRequest {
    std::string id;
    std::string name;
    nlohmann::json arguments;
};

struct ToolCallRecord {
    std::string id;
    std::string name;
    nlohmann::json arguments;
    nlohmann::json result;
    bool approved = true;
    std::string approval_detail;
};

struct FileChangeRecord {
    std::string path;
    std::string action;
    std::optional<std::string> old_hash;
    std::optional<std::string> new_hash;
};

// ─── Turn ──────────────────────────────────────────────────────────────────

struct Turn {
    std::string id;
    TurnState state = TurnState::Running;
    std::string user_input;
    std::string assistant_content;
    std::vector<ToolCallRequest> tool_requests;
    std::vector<ToolCallRecord> tool_results;
    std::vector<FileChangeRecord> file_changes;
    TokenUsage usage;
    std::string provider;
    std::string model;
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point completed_at;
    std::string error_message;
};

// ─── Thread ────────────────────────────────────────────────────────────────

struct Thread {
    std::string id;
    std::string title;
    std::string provider;
    std::string model;
    std::string workspace;
    std::vector<Turn> turns;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    std::optional<std::string> forked_from;
};

// ─── JSONL event log ───────────────────────────────────────────────────────

struct EventLogEntry {
    std::string type;
    std::string thread_id;
    std::string turn_id;
    nlohmann::json payload;
};

// ─── JSON serialization ───────────────────────────────────────────────────

void to_json(nlohmann::json& j, const TokenUsage& u);
void from_json(const nlohmann::json& j, TokenUsage& u);

void to_json(nlohmann::json& j, const ToolCallRequest& r);
void from_json(const nlohmann::json& j, ToolCallRequest& r);

void to_json(nlohmann::json& j, const ToolCallRecord& r);
void from_json(const nlohmann::json& j, ToolCallRecord& r);

void to_json(nlohmann::json& j, const FileChangeRecord& r);
void from_json(const nlohmann::json& j, FileChangeRecord& r);

void to_json(nlohmann::json& j, const Turn& t);
void from_json(const nlohmann::json& j, Turn& t);

void to_json(nlohmann::json& j, const Thread& t);
void from_json(const nlohmann::json& j, Thread& t);

void to_json(nlohmann::json& j, const EventLogEntry& e);
void from_json(const nlohmann::json& j, EventLogEntry& e);

// ─── Utilities ─────────────────────────────────────────────────────────────

/// Serialize thread to JSONL string.
std::string threadToJsonl(const Thread& thread);

/// Parse JSONL event log into a Thread.
std::optional<Thread> jsonlToThread(const std::string& jsonl);

/// Migrate old-format session JSON into a Thread.
std::optional<Thread> migrateLegacySession(
    const nlohmann::json& legacy,
    const std::string& thread_id,
    const std::string& provider,
    const std::string& model
);

/// Find the recovery point: index of the first tool call whose result is
/// missing (i.e. may need re-execution after crash). Returns turn size if
/// all tool calls have results.
std::size_t recoveryPoint(const Turn& turn);

} // namespace opencode

#endif
