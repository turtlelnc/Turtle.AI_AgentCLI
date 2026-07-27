#include "session_model.hpp"

#include <chrono>
#include <sstream>

namespace opencode {

// ─── JSON serialization helpers ───────────────────────────────────────────

static std::string timeToStr(std::chrono::system_clock::time_point tp) {
    const auto t = std::chrono::system_clock::to_time_t(tp);
    std::ostringstream os;
    os << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
    return os.str();
}

static std::chrono::system_clock::time_point strToTime(const std::string& s) {
    std::tm tm{};
    std::istringstream is(s);
    is >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (is.fail()) return {};
#if defined(_WIN32)
    return std::chrono::system_clock::from_time_t(_mkgmtime(&tm));
#else
    return std::chrono::system_clock::from_time_t(timegm(&tm));
#endif
}

// ─── TokenUsage ────────────────────────────────────────────────────────────

void to_json(nlohmann::json& j, const TokenUsage& u) {
    j = {{"input_tokens", u.input_tokens},
         {"output_tokens", u.output_tokens},
         {"cost_usd", u.cost_usd}};
}
void from_json(const nlohmann::json& j, TokenUsage& u) {
    j.at("input_tokens").get_to(u.input_tokens);
    j.at("output_tokens").get_to(u.output_tokens);
    u.cost_usd = j.value("cost_usd", 0.0);
}

// ─── ToolCallRequest ───────────────────────────────────────────────────────

void to_json(nlohmann::json& j, const ToolCallRequest& r) {
    j = {{"id", r.id}, {"name", r.name}, {"arguments", r.arguments}};
}
void from_json(const nlohmann::json& j, ToolCallRequest& r) {
    j.at("id").get_to(r.id);
    j.at("name").get_to(r.name);
    r.arguments = j.value("arguments", nlohmann::json::object());
}

// ─── ToolCallRecord ────────────────────────────────────────────────────────

void to_json(nlohmann::json& j, const ToolCallRecord& r) {
    j = {{"id", r.id}, {"name", r.name}, {"arguments", r.arguments},
         {"result", r.result}, {"approved", r.approved},
         {"approval_detail", r.approval_detail}};
}
void from_json(const nlohmann::json& j, ToolCallRecord& r) {
    j.at("id").get_to(r.id);
    j.at("name").get_to(r.name);
    r.arguments = j.value("arguments", nlohmann::json::object());
    r.result = j.value("result", nlohmann::json::object());
    r.approved = j.value("approved", true);
    r.approval_detail = j.value("approval_detail", "");
}

// ─── FileChangeRecord ─────────────────────────────────────────────────────

void to_json(nlohmann::json& j, const FileChangeRecord& r) {
    j = {{"path", r.path}, {"action", r.action}};
    if (r.old_hash) j["old_hash"] = *r.old_hash;
    if (r.new_hash) j["new_hash"] = *r.new_hash;
}
void from_json(const nlohmann::json& j, FileChangeRecord& r) {
    j.at("path").get_to(r.path);
    j.at("action").get_to(r.action);
    if (j.contains("old_hash")) r.old_hash = j["old_hash"].get<std::string>();
    if (j.contains("new_hash")) r.new_hash = j["new_hash"].get<std::string>();
}

// ─── Turn ──────────────────────────────────────────────────────────────────

void to_json(nlohmann::json& j, const Turn& t) {
    j = {
        {"id", t.id},
        {"state", turnStateName(t.state)},
        {"user_input", t.user_input},
        {"assistant_content", t.assistant_content},
        {"tool_requests", t.tool_requests},
        {"tool_results", t.tool_results},
        {"file_changes", t.file_changes},
        {"usage", t.usage},
        {"provider", t.provider},
        {"model", t.model},
        {"started_at", timeToStr(t.started_at)},
        {"completed_at", timeToStr(t.completed_at)},
        {"error_message", t.error_message}
    };
}
void from_json(const nlohmann::json& j, Turn& t) {
    j.at("id").get_to(t.id);
    std::string state_str = j.value("state", "running");
    if (state_str == "completed") t.state = TurnState::Completed;
    else if (state_str == "failed") t.state = TurnState::Failed;
    else if (state_str == "cancelled") t.state = TurnState::Cancelled;
    else t.state = TurnState::Running;
    t.user_input = j.value("user_input", "");
    t.assistant_content = j.value("assistant_content", "");
    if (j.contains("tool_requests"))
        j.at("tool_requests").get_to(t.tool_requests);
    if (j.contains("tool_results"))
        j.at("tool_results").get_to(t.tool_results);
    if (j.contains("file_changes"))
        j.at("file_changes").get_to(t.file_changes);
    if (j.contains("usage")) j.at("usage").get_to(t.usage);
    t.provider = j.value("provider", "");
    t.model = j.value("model", "");
    if (j.contains("started_at"))
        t.started_at = strToTime(j["started_at"].get<std::string>());
    if (j.contains("completed_at"))
        t.completed_at = strToTime(j["completed_at"].get<std::string>());
    t.error_message = j.value("error_message", "");
}

// ─── Thread ────────────────────────────────────────────────────────────────

void to_json(nlohmann::json& j, const Thread& t) {
    j = {
        {"id", t.id},
        {"title", t.title},
        {"provider", t.provider},
        {"model", t.model},
        {"workspace", t.workspace},
        {"turns", t.turns},
        {"created_at", timeToStr(t.created_at)},
        {"updated_at", timeToStr(t.updated_at)}
    };
    if (t.forked_from) j["forked_from"] = *t.forked_from;
}
void from_json(const nlohmann::json& j, Thread& t) {
    j.at("id").get_to(t.id);
    t.title = j.value("title", "");
    t.provider = j.value("provider", "");
    t.model = j.value("model", "");
    t.workspace = j.value("workspace", "");
    if (j.contains("turns")) j.at("turns").get_to(t.turns);
    if (j.contains("created_at"))
        t.created_at = strToTime(j["created_at"].get<std::string>());
    if (j.contains("updated_at"))
        t.updated_at = strToTime(j["updated_at"].get<std::string>());
    if (j.contains("forked_from"))
        t.forked_from = j["forked_from"].get<std::string>();
}

// ─── EventLogEntry ─────────────────────────────────────────────────────────

void to_json(nlohmann::json& j, const EventLogEntry& e) {
    j = {{"type", e.type}, {"thread_id", e.thread_id},
         {"turn_id", e.turn_id}, {"payload", e.payload}};
}
void from_json(const nlohmann::json& j, EventLogEntry& e) {
    j.at("type").get_to(e.type);
    e.thread_id = j.value("thread_id", "");
    e.turn_id = j.value("turn_id", "");
    e.payload = j.value("payload", nlohmann::json::object());
}

// ─── Utilities ─────────────────────────────────────────────────────────────

std::string threadToJsonl(const Thread& thread) {
    nlohmann::json tj = thread;
    // Emit one event per turn for fine-grained recovery.
    std::ostringstream out;
    // Header: thread metadata (reconstructible from first line).
    nlohmann::json meta;
    meta["type"] = "thread_created";
    meta["thread_id"] = thread.id;
    nlohmann::json p;
    p["title"] = thread.title;
    p["provider"] = thread.provider;
    p["model"] = thread.model;
    p["workspace"] = thread.workspace;
    p["created_at"] = timeToStr(thread.created_at);
    if (thread.forked_from) p["forked_from"] = *thread.forked_from;
    meta["payload"] = std::move(p);
    out << meta.dump() << '\n';

    // One event per turn.
    for (const auto& turn : thread.turns) {
        nlohmann::json evt;
        evt["type"] = "turn";
        evt["thread_id"] = thread.id;
        evt["turn_id"] = turn.id;
        evt["payload"] = turn;
        out << evt.dump() << '\n';
    }
    return out.str();
}

std::optional<Thread> jsonlToThread(const std::string& jsonl) {
    Thread thread;
    std::istringstream input(jsonl);
    std::string line;
    bool have_meta = false;

    while (std::getline(input, line)) {
        if (line.empty()) continue;
        nlohmann::json entry;
        try { entry = nlohmann::json::parse(line); }
        catch (...) { continue; }

        const std::string type = entry.value("type", "");

        if (type == "thread_created") {
            thread.id = entry.value("thread_id", "");
            const auto& p = entry["payload"];
            thread.title = p.value("title", "");
            thread.provider = p.value("provider", "");
            thread.model = p.value("model", "");
            thread.workspace = p.value("workspace", "");
            if (p.contains("created_at"))
                thread.created_at = strToTime(p["created_at"].get<std::string>());
            thread.updated_at = thread.created_at;
            if (p.contains("forked_from"))
                thread.forked_from = p["forked_from"].get<std::string>();
            have_meta = true;
        } else if (type == "turn" && have_meta) {
            Turn turn;
            try { entry.at("payload").get_to(turn); }
            catch (...) { continue; }
            if (turn.id.empty()) turn.id = entry.value("turn_id", "");
            thread.turns.push_back(std::move(turn));
            thread.updated_at = thread.turns.back().completed_at;
        }
    }

    if (!have_meta) return std::nullopt;
    return thread;
}

std::optional<Thread> migrateLegacySession(
    const nlohmann::json& legacy,
    const std::string& thread_id,
    const std::string& provider,
    const std::string& model
) {
    if (!legacy.is_array()) return std::nullopt;

    Thread thread;
    thread.id = thread_id;
    thread.provider = provider;
    thread.model = model;
    thread.title = "Migrated session";
    thread.workspace = ".";
    thread.created_at = std::chrono::system_clock::now();
    thread.updated_at = thread.created_at;

    // Legacy format: array of {role, content, tool_calls, ...}
    // We group consecutive user→assistant(+tools) exchanges into turns.
    Turn current_turn;
    bool in_turn = false;

    for (const auto& msg : legacy) {
        if (!msg.contains("role")) continue;
        const std::string role = msg["role"].get<std::string>();

        if (role == "system") {
            // Skip — system prompt is reconstructed by AgentSession.
            continue;
        }

        if (role == "user") {
            if (in_turn) {
                thread.turns.push_back(std::move(current_turn));
                current_turn = Turn{};
            }
            current_turn.id = "turn-" + std::to_string(thread.turns.size());
            current_turn.state = TurnState::Completed;
            current_turn.user_input = msg.value("content", "");
            current_turn.started_at = thread.created_at;
            current_turn.completed_at = thread.created_at;
            current_turn.provider = provider;
            current_turn.model = model;
            in_turn = true;
        } else if (role == "assistant" && in_turn) {
            current_turn.assistant_content = msg.value("content", "");
            if (msg.contains("tool_calls")) {
                for (const auto& tc : msg["tool_calls"]) {
                    ToolCallRequest req;
                    req.id = tc.value("id", "");
                    req.name = tc.value("name", tc.value("function", nlohmann::json::object()).value("name", ""));
                    req.arguments = tc.value("arguments", tc.value("input", nlohmann::json::object()));
                    current_turn.tool_requests.push_back(std::move(req));
                }
            }
        } else if (role == "tool" && in_turn) {
            ToolCallRecord rec;
            rec.id = msg.value("tool_call_id", "");
            rec.name = "";
            rec.approved = true;
            rec.result = {{"content", msg.value("content", "")}};
            current_turn.tool_results.push_back(std::move(rec));
        }
    }
    if (in_turn) {
        thread.turns.push_back(std::move(current_turn));
    }

    return thread;
}

std::size_t recoveryPoint(const Turn& turn) {
    // A turn is fully recoverable if every tool_request has a corresponding
    // tool_result.  Return the index of the first unrecovered tool.
    if (turn.tool_requests.empty()) return 0;

    // Match by id.
    for (std::size_t i = 0; i < turn.tool_requests.size(); ++i) {
        bool found = false;
        for (const auto& result : turn.tool_results) {
            if (result.id == turn.tool_requests[i].id) {
                found = true;
                break;
            }
        }
        if (!found) return i;  // This tool was never executed → recover from here.
    }
    return turn.tool_requests.size();  // All complete.
}

} // namespace opencode
