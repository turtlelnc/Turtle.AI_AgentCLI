#include "config_manager.hpp"
#include "http_client.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <ctime>
#include <sys/stat.h>
#include <cstdlib>
#include <filesystem>

namespace opencode {

ConfigManager::ConfigManager() {
    config_.provider = ProviderType::Unknown;
    config_.total_input_tokens = 0;
    config_.total_output_tokens = 0;
    config_.total_cost_usd = 0.0;
}

std::string ConfigManager::homeDirectory() {
    const char* home = std::getenv("HOME");
    if (!home) {
        home = std::getenv("USERPROFILE");  // Windows
    }
    if (!home) {
        return ".";
    }
    return home;
}

std::string ConfigManager::getDefaultConfigPath() const {
    return homeDirectory() + "/.turtle/config.json";
}

bool ConfigManager::loadDefaultConfig() {
    const std::string current = getDefaultConfigPath();
    const std::string legacy = homeDirectory() + "/.opencode_config.json";
    bool loaded = false;
    if (std::filesystem::exists(current)) {
        loaded = loadConfig(current);
        if (std::filesystem::exists(legacy)) {
            try {
                nlohmann::json old_value;
                std::ifstream old_input(legacy);
                old_input >> old_value;
                const std::string old_key = old_value.value("api_key", "");
                if (!old_key.empty() && !legacy_api_key_) {
                    legacy_api_key_ = old_key;
                    legacy_key_provider_ = parseProvider(
                        old_value.value("provider", "unknown")
                    );
                    legacy_credential_files_.push_back(legacy);
                }
            } catch (...) {
                // Keep current configuration usable. Malformed legacy data
                // is never trusted or deleted automatically.
            }
        }
    } else if (std::filesystem::exists(legacy)) {
        loaded = loadConfig(legacy);
        if (loaded) loaded = saveConfig(current);
    }

    // Migrate the obsolete dotenv file without ever writing a new dotenv.
    const std::filesystem::path legacy_env =
        homeDirectory() + "/.opencode_env";
    if (std::filesystem::exists(legacy_env)) {
        std::ifstream input(legacy_env);
        std::string line;
        while (std::getline(input, line)) {
            constexpr const char* prefix = "OPENCODE_API_KEY=";
            if (line.rfind(prefix, 0) == 0) {
                const std::string value = line.substr(std::char_traits<char>::length(prefix));
                if (!value.empty() && !legacy_api_key_) {
                    legacy_api_key_ = value;
                    legacy_key_provider_ = config_.provider;
                }
            }
        }
        legacy_credential_files_.push_back(legacy_env.string());
    }
    return loaded;
}

bool ConfigManager::loadConfig(const std::string& path) {
    config_path_ = path;
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    
    try {
        nlohmann::json j;
        file >> j;
        
        config_.api_url = j.value("api_url", "");
        const std::string legacy_key = j.value("api_key", "");
        if (!legacy_key.empty()) {
            legacy_api_key_ = legacy_key;
            legacy_credential_files_.push_back(path);
        }
        config_.model = j.value("model", "");
        config_.subagent_enabled = j.value("subagent_enabled", false);
        config_.subagent_provider = parseProvider(
            j.value("subagent_provider", "unknown")
        );
        config_.subagent_api_url = j.value("subagent_api_url", "");
        config_.subagent_model = j.value("subagent_model", "");
        config_.work_dir = j.value("work_dir", ".");
        config_.use_git = j.value("use_git", false);
        
        std::string provider_str = j.value("provider", "unknown");
        config_.provider = parseProvider(provider_str);
        if (legacy_api_key_) legacy_key_provider_ = config_.provider;
        
        // 加载历史记录
        if (j.contains("workspace_history")) {
            for (const auto& entry : j["workspace_history"]) {
                WorkspaceHistoryEntry e;
                e.path = entry.value("path", "");
                e.last_used = entry.value("last_used", static_cast<int64_t>(0));
                config_.workspace_history.push_back(e);
            }
        }
        
        if (j.contains("conversations")) {
            for (const auto& conv : j["conversations"]) {
                ConversationRecord c;
                c.name = conv.value("name", "");
                c.file_path = conv.value("file_path", "");
                c.created_at = conv.value("created_at", static_cast<int64_t>(0));
                c.updated_at = conv.value("updated_at", static_cast<int64_t>(0));
                config_.conversations.push_back(c);
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading config: " << e.what() << std::endl;
        return false;
    }
}

bool ConfigManager::saveConfig(const std::string& path) {
    std::error_code directory_error;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, directory_error);
        if (directory_error) return false;
    }
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    
    try {
        nlohmann::json j;
        j["api_url"] = config_.api_url;
        j["model"] = config_.model;
        j["subagent_enabled"] = config_.subagent_enabled;
        j["subagent_provider"] = providerToString(config_.subagent_provider);
        j["subagent_api_url"] = config_.subagent_api_url;
        j["subagent_model"] = config_.subagent_model;
        j["work_dir"] = config_.work_dir;
        j["use_git"] = config_.use_git;
        j["provider"] = providerToString(config_.provider);
        
        // 保存历史记录
        j["workspace_history"] = nlohmann::json::array();
        for (const auto& entry : config_.workspace_history) {
            nlohmann::json e;
            e["path"] = entry.path;
            e["last_used"] = entry.last_used;
            j["workspace_history"].push_back(e);
        }
        
        j["conversations"] = nlohmann::json::array();
        for (const auto& conv : config_.conversations) {
            nlohmann::json c;
            c["name"] = conv.name;
            c["file_path"] = conv.file_path;
            c["created_at"] = conv.created_at;
            c["updated_at"] = conv.updated_at;
            j["conversations"].push_back(c);
        }
        
        file << j.dump(2);
        file.close();
        std::error_code permission_error;
        std::filesystem::permissions(
            path,
            std::filesystem::perms::owner_read |
                std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace,
            permission_error
        );
        return !permission_error;
    } catch (const std::exception& e) {
        std::cerr << "Error saving config: " << e.what() << std::endl;
        return false;
    }
}

Config& ConfigManager::getConfig() {
    return config_;
}

const Config& ConfigManager::getConfig() const {
    return config_;
}

ProviderType ConfigManager::parseProvider(const std::string& name) {
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    
    if (lower_name.find("openaicompatible") != std::string::npos ||
        lower_name.find("openai-compatible") != std::string::npos)
        return ProviderType::OpenAICompatible;
    if (lower_name.find("deepseek") != std::string::npos) return ProviderType::DeepSeek;
    if (lower_name.find("openai") != std::string::npos) return ProviderType::OpenAI;
    if (lower_name.find("anthropic") != std::string::npos || lower_name.find("claude") != std::string::npos) 
        return ProviderType::Anthropic;
    if (lower_name.find("llama.cpp") != std::string::npos || lower_name.find("llamacpp") != std::string::npos ||
        lower_name.find("ollama") != std::string::npos || lower_name.find("localhost") != std::string::npos)
        return ProviderType::LlamaCpp;
    
    return ProviderType::Unknown;
}

std::string ConfigManager::providerToString(ProviderType p) {
    switch (p) {
        case ProviderType::DeepSeek: return "DeepSeek";
        case ProviderType::OpenAI: return "OpenAI";
        case ProviderType::Anthropic: return "Anthropic";
        case ProviderType::LlamaCpp: return "LlamaCpp";
        case ProviderType::OpenAICompatible: return "OpenAICompatible";
        default: return "Unknown";
    }
}

bool ConfigManager::validate() const {
    if (config_.provider == ProviderType::Unknown) {
        return false;
    }
    
    if (config_.provider != ProviderType::LlamaCpp &&
        config_.provider != ProviderType::OpenAICompatible &&
        config_.api_key.empty()) {
        return false;
    }
    
    if (config_.api_url.empty()) {
        return false;
    }
    if (config_.model.empty()) {
        return false;
    }
    if (config_.subagent_enabled &&
        (config_.subagent_provider == ProviderType::Unknown ||
         config_.subagent_api_url.empty() ||
         config_.subagent_model.empty())) {
        return false;
    }
    
    return true;
}

void ConfigManager::addWorkspaceToHistory(const std::string& path) {
    // 检查是否已存在
    for (auto& entry : config_.workspace_history) {
        if (entry.path == path) {
            entry.last_used = static_cast<int64_t>(std::time(nullptr));
            return;
        }
    }
    
    // 添加新条目
    WorkspaceHistoryEntry entry;
    entry.path = path;
    entry.last_used = static_cast<int64_t>(std::time(nullptr));
    config_.workspace_history.push_back(entry);
    
    // 限制历史记录数量（最多保留 15 个）
    if (config_.workspace_history.size() > 15) {
        config_.workspace_history.erase(config_.workspace_history.begin());
    }
}

std::vector<WorkspaceHistoryEntry> ConfigManager::getRecentWorkspaces(int limit, int days) const {
    std::vector<WorkspaceHistoryEntry> result;
    int64_t cutoff = static_cast<int64_t>(std::time(nullptr)) - (days * 24 * 60 * 60);
    
    for (const auto& entry : config_.workspace_history) {
        if (entry.last_used >= cutoff) {
            result.push_back(entry);
        }
    }
    
    // 按时间倒序排序（最近的在前）
    std::sort(result.begin(), result.end(), [](const WorkspaceHistoryEntry& a, const WorkspaceHistoryEntry& b) {
        return a.last_used > b.last_used;
    });
    
    // 限制返回数量
    if (static_cast<int>(result.size()) > limit) {
        result.resize(static_cast<size_t>(limit));
    }
    
    return result;
}

void ConfigManager::addConversation(const ConversationRecord& record) {
    config_.conversations.push_back(record);
}

std::vector<ConversationRecord> ConfigManager::getConversations() const {
    return config_.conversations;
}

bool ConfigManager::loadConversation(const std::string& file_path, std::vector<ChatMessage>& messages) const {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return false;
    }
    
    try {
        nlohmann::json j;
        file >> j;
        
        if (!j.contains("messages") || !j["messages"].is_array()) {
            return false;
        }
        
        for (const auto& msg : j["messages"]) {
            ChatMessage m;
            m.role = msg.value("role", "");
            m.content = msg.value("content", "");
            if (msg.contains("content_blocks") && !msg["content_blocks"].is_null()) {
                m.content_blocks = msg["content_blocks"];
            }
            messages.push_back(m);
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading conversation: " << e.what() << std::endl;
        return false;
    }
}

bool ConfigManager::saveConversation(const std::string& file_path, const std::vector<ChatMessage>& messages) {
    try {
        nlohmann::json j;
        j["messages"] = nlohmann::json::array();
        
        for (const auto& msg : messages) {
            nlohmann::json m;
            m["role"] = msg.role;
            m["content"] = msg.content;
            if (!msg.content_blocks.is_null()) {
                m["content_blocks"] = msg.content_blocks;
            }
            j["messages"].push_back(m);
        }
        
        std::ofstream file(file_path);
        if (!file.is_open()) {
            return false;
        }
        
        file << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving conversation: " << e.what() << std::endl;
        return false;
    }
}

std::string ConfigManager::maskApiKey(const std::string& api_key) {
    if (api_key.length() <= 16) {
        return "****";
    }
    return api_key.substr(0, 8) + "********" + api_key.substr(api_key.length() - 4);
}

std::optional<std::string> ConfigManager::takeLegacyApiKey(
    ProviderType provider
) {
    if (!legacy_api_key_ ||
        (legacy_key_provider_ != ProviderType::Unknown &&
         legacy_key_provider_ != provider)) {
        return std::nullopt;
    }
    auto value = std::move(legacy_api_key_);
    legacy_api_key_.reset();
    return value;
}

bool ConfigManager::hasLegacyApiKey() const {
    return legacy_api_key_.has_value();
}

bool ConfigManager::sanitizeLegacyCredentials() {
    bool success = true;
    for (const auto& file_name : legacy_credential_files_) {
        const std::filesystem::path path(file_name);
        if (!std::filesystem::exists(path)) continue;
        if (path.extension() == ".json") {
            try {
                nlohmann::json value;
                {
                    std::ifstream input(path);
                    input >> value;
                }
                value.erase("api_key");
                std::ofstream output(path, std::ios::trunc);
                if (!output) {
                    success = false;
                    continue;
                }
                output << value.dump(2);
            } catch (...) {
                success = false;
                continue;
            }
        } else {
            std::ifstream input(path);
            std::string sanitized;
            std::string line;
            while (std::getline(input, line)) {
                if (line.rfind("OPENCODE_API_KEY=", 0) != 0) {
                    sanitized += line + '\n';
                }
            }
            std::ofstream output(path, std::ios::trunc);
            if (!output) {
                success = false;
                continue;
            }
            output << sanitized;
        }
        std::error_code error;
        std::filesystem::permissions(
            path,
            std::filesystem::perms::owner_read |
                std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace,
            error
        );
        if (error) success = false;
    }
    if (success) legacy_credential_files_.clear();
    return success;
}

// ─── Named configuration profiles ────────────────────────────────────────

bool ConfigManager::setProfile(const std::string& name) {
    if (name.empty() || name.find('/') != std::string::npos) return false;
    const auto path = getDefaultConfigPath();
    nlohmann::json root;
    try {
        std::ifstream input(path);
        if (input) input >> root;
    } catch (...) { /* keep empty root on parse error */ }
    if (!root.contains("profiles") || !root["profiles"].contains(name)) {
        // Create the profile as a copy of current config.
        if (!root.contains("profiles")) root["profiles"] = nlohmann::json::object();
        root["profiles"][name] = {
            {"provider", providerToString(config_.provider)},
            {"api_url", config_.api_url},
            {"model", config_.model},
            {"subagent_enabled", config_.subagent_enabled},
            {"subagent_provider", providerToString(config_.subagent_provider)},
            {"subagent_api_url", config_.subagent_api_url},
            {"subagent_model", config_.subagent_model},
            {"work_dir", config_.work_dir},
            {"use_git", config_.use_git}
        };
        root["active_profile"] = name;
        std::ofstream out(path);
        if (!out) return false;
        out << root.dump(2);
    } else {
        root["active_profile"] = name;
        std::ofstream out(path);
        if (!out) return false;
        out << root.dump(2);
    }
    active_profile_ = name;
    return true;
}

std::vector<std::string> ConfigManager::listProfiles() const {
    std::vector<std::string> names{"default"};
    const auto path = getDefaultConfigPath();
    std::ifstream input(path);
    if (!input) return names;
    try {
        nlohmann::json root;
        input >> root;
        if (root.contains("profiles")) {
            for (auto& [k, _] : root["profiles"].items()) names.push_back(k);
        }
    } catch (...) {}
    return names;
}

} // namespace opencode
