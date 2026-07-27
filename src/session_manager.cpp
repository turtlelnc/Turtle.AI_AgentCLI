#include "session_manager.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace opencode {

namespace {

constexpr std::uintmax_t kMaxSessionBytes = 20 * 1024 * 1024;
constexpr std::size_t kMaxMessages = 2000;

nlohmann::json toolCallToJson(const ToolCall& call) {
    return {
        {"id", call.id},
        {"type", call.type},
        {"name", call.name},
        {"arguments", call.arguments},
        {"input", call.input}
    };
}

ToolCall toolCallFromJson(const nlohmann::json& value) {
    ToolCall call;
    call.id = value.value("id", "");
    call.type = value.value("type", "");
    call.name = value.value("name", "");
    if (value.contains("arguments")) call.arguments = value["arguments"];
    if (value.contains("input")) call.input = value["input"];
    return call;
}

std::string deriveTitle(const std::vector<ChatMessage>& messages) {
    for (const auto& message : messages) {
        if (message.role != "user" || message.content.empty()) continue;
        std::string title = message.content;
        std::replace(title.begin(), title.end(), '\n', ' ');
        if (title.size() > 60) title = title.substr(0, 57) + "...";
        return title;
    }
    return "New session";
}

} // namespace

bool SessionManager::setWorkspace(const std::filesystem::path& workspace) {
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(workspace, error);
    if (error || !std::filesystem::is_directory(root, error) || error) return false;
    sessions_root_ = root / ".turtle" / "sessions";
    return true;
}

bool SessionManager::validId(const std::string& id) {
    if (id.empty() || id.size() > 80) return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_';
    });
}

std::string SessionManager::create(
    const std::string&,
    const std::string&
) {
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();
    return "session-" + std::to_string(millis);
}

bool SessionManager::save(
    const std::string& id,
    const std::string& provider,
    const std::string& model,
    const std::vector<ChatMessage>& messages,
    std::string* error_message
) const {
    if (!validId(id) || sessions_root_.empty()) {
        if (error_message) *error_message = "Invalid session id or workspace";
        return false;
    }
    if (messages.size() > kMaxMessages) {
        if (error_message) *error_message = "Session exceeds 2000 messages";
        return false;
    }
    try {
        nlohmann::json root = {
            {"version", 1},
            {"id", id},
            {"title", deriveTitle(messages)},
            {"provider", provider},
            {"model", model},
            {"updated_at", static_cast<std::int64_t>(std::time(nullptr))},
            {"messages", nlohmann::json::array()}
        };
        for (const auto& message : messages) {
            nlohmann::json item = {
                {"role", message.role},
                {"content", message.content},
                {"content_blocks", message.content_blocks},
                {"tool_call_id", message.tool_call_id},
                {"tool_calls", nlohmann::json::array()}
            };
            for (const auto& call : message.tool_calls) {
                item["tool_calls"].push_back(toolCallToJson(call));
            }
            root["messages"].push_back(std::move(item));
        }

        std::error_code error;
        std::filesystem::create_directories(sessions_root_, error);
        if (error) throw std::runtime_error(error.message());
        std::filesystem::permissions(
            sessions_root_,
            std::filesystem::perms::owner_all,
            std::filesystem::perm_options::replace,
            error
        );
        if (error) throw std::runtime_error(error.message());
        const auto destination = sessions_root_ / (id + ".json");
        const auto temporary = sessions_root_ / (id + ".tmp");
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Cannot open temporary session file");
            output << root.dump(2);
            if (!output) throw std::runtime_error("Cannot write session file");
        }
        std::filesystem::rename(temporary, destination, error);
        if (error) {
            std::filesystem::remove(destination, error);
            error.clear();
            std::filesystem::rename(temporary, destination, error);
        }
        if (error) throw std::runtime_error(error.message());
        std::filesystem::permissions(
            destination,
            std::filesystem::perms::owner_read |
                std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace,
            error
        );
        if (error) throw std::runtime_error(error.message());
        return true;
    } catch (const std::exception& e) {
        if (error_message) *error_message = e.what();
        return false;
    }
}

bool SessionManager::load(
    const std::string& id,
    std::vector<ChatMessage>& messages,
    SessionInfo& info,
    std::string* error_message
) const {
    if (!validId(id) || sessions_root_.empty()) {
        if (error_message) *error_message = "Invalid session id or workspace";
        return false;
    }
    try {
        const auto path = sessions_root_ / (id + ".json");
        std::error_code error;
        if (std::filesystem::file_size(path, error) > kMaxSessionBytes || error) {
            throw std::runtime_error(error ? "Cannot inspect session file"
                                           : "Session exceeds 20 MiB");
        }
        std::ifstream input(path);
        if (!input) throw std::runtime_error("Session not found: " + id);
        nlohmann::json root;
        input >> root;
        if (root.value("version", 0) != 1 ||
            !root.contains("messages") || !root["messages"].is_array() ||
            root["messages"].size() > kMaxMessages) {
            throw std::runtime_error("Unsupported or invalid session format");
        }
        std::vector<ChatMessage> loaded;
        for (const auto& item : root["messages"]) {
            ChatMessage message;
            message.role = item.value("role", "");
            message.content = item.value("content", "");
            if (item.contains("content_blocks")) {
                message.content_blocks = item["content_blocks"];
            }
            message.tool_call_id = item.value("tool_call_id", "");
            if (item.contains("tool_calls") && item["tool_calls"].is_array()) {
                for (const auto& call : item["tool_calls"]) {
                    message.tool_calls.push_back(toolCallFromJson(call));
                }
            }
            if (message.role.empty()) throw std::runtime_error("Message has no role");
            loaded.push_back(std::move(message));
        }
        info.id = id;
        info.title = root.value("title", "Untitled session");
        info.provider = root.value("provider", "");
        info.model = root.value("model", "");
        info.updated_at = root.value("updated_at", static_cast<std::int64_t>(0));
        info.message_count = loaded.size();
        messages = std::move(loaded);
        return true;
    } catch (const std::exception& e) {
        if (error_message) *error_message = e.what();
        return false;
    }
}

std::vector<SessionInfo> SessionManager::list() const {
    std::vector<SessionInfo> sessions;
    std::error_code error;
    if (!std::filesystem::is_directory(sessions_root_, error) || error) return sessions;
    for (std::filesystem::directory_iterator it(sessions_root_, error), end;
         !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || it->path().extension() != ".json") continue;
        const std::string id = it->path().stem().string();
        SessionInfo info;
        std::vector<ChatMessage> ignored;
        if (load(id, ignored, info)) sessions.push_back(std::move(info));
    }
    std::sort(sessions.begin(), sessions.end(), [](const auto& left, const auto& right) {
        return left.updated_at > right.updated_at;
    });
    return sessions;
}

// ─── Structured (Thread/Turn-based) ────────────────────────────────────────

bool SessionManager::saveThread(
    const Thread& thread, std::string* error_message
) const {
    if (!validId(thread.id) || sessions_root_.empty()) {
        if (error_message) *error_message = "Invalid thread id or workspace";
        return false;
    }
    try {
        const std::string jsonl = threadToJsonl(thread);
        std::error_code ec;
        std::filesystem::create_directories(sessions_root_, ec);
        if (ec) throw std::runtime_error(ec.message());

        const auto dest = sessions_root_ / (thread.id + ".jsonl");
        const auto tmp = sessions_root_ / (thread.id + ".tmp");
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) throw std::runtime_error("Cannot open thread file");
            out << jsonl;
            if (!out) throw std::runtime_error("Cannot write thread file");
        }
        std::filesystem::rename(tmp, dest, ec);
        if (ec) { std::filesystem::remove(dest, ec); std::filesystem::rename(tmp, dest, ec); }
        if (ec) throw std::runtime_error(ec.message());
        std::filesystem::permissions(dest,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace, ec);
        return true;
    } catch (const std::exception& e) {
        if (error_message) *error_message = e.what();
        return false;
    }
}

std::optional<Thread> SessionManager::loadThread(
    const std::string& id, std::string* error_message
) const {
    if (!validId(id) || sessions_root_.empty()) {
        if (error_message) *error_message = "Invalid thread id or workspace";
        return std::nullopt;
    }
    try {
        const auto path = sessions_root_ / (id + ".jsonl");
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || ec) {
            // Try legacy .json format.
            const auto legacy_path = sessions_root_ / (id + ".json");
            if (std::filesystem::is_regular_file(legacy_path, ec) && !ec) {
                std::ifstream input(legacy_path);
                if (!input) throw std::runtime_error("Session not found: " + id);
                nlohmann::json root;
                input >> root;
                std::string provider = root.value("provider", "");
                std::string model = root.value("model", "");
                auto thread = migrateLegacySession(root["messages"], id, provider, model);
                if (thread) return thread;
                throw std::runtime_error("Cannot migrate legacy session");
            }
            throw std::runtime_error("Thread not found: " + id);
        }
        if (std::filesystem::file_size(path, ec) > 20 * 1024 * 1024 || ec)
            throw std::runtime_error("Thread file too large or unreadable");

        std::ifstream input(path);
        std::ostringstream content;
        content << input.rdbuf();
        return jsonlToThread(content.str());
    } catch (const std::exception& e) {
        if (error_message) *error_message = e.what();
        return std::nullopt;
    }
}

std::optional<Thread> SessionManager::forkThread(
    const std::string& source_id,
    const std::string& new_id,
    std::string* error_message
) const {
    auto source = loadThread(source_id, error_message);
    if (!source) return std::nullopt;
    Thread forked = std::move(*source);
    forked.id = new_id;
    forked.forked_from = source_id;
    forked.created_at = std::chrono::system_clock::now();
    forked.updated_at = forked.created_at;
    if (!saveThread(forked, error_message)) return std::nullopt;
    return forked;
}

} // namespace opencode
