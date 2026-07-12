#include "config_manager.hpp"
#include "http_client.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <ctime>
#include <sys/stat.h>
#include <cstdlib>

namespace opencode {

ConfigManager::ConfigManager() {
    config_.provider = ProviderType::Unknown;
    config_.total_input_tokens = 0;
    config_.total_output_tokens = 0;
    config_.total_cost_usd = 0.0;
}

std::string ConfigManager::getGlobalConfigPath() {
    const char* home = std::getenv("HOME");
    if (!home) {
        home = std::getenv("USERPROFILE");  // Windows
    }
    if (!home) {
        return ".opencode_config.json";
    }
    return std::string(home) + "/.opencode_config.json";
}

std::string ConfigManager::getEnvFilePath() {
    const char* home = std::getenv("HOME");
    if (!home) {
        home = std::getenv("USERPROFILE");  // Windows
    }
    if (!home) {
        return ".opencode_env";
    }
    return std::string(home) + "/.opencode_env";
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
        config_.api_key = j.value("api_key", "");
        config_.model = j.value("model", "");
        config_.work_dir = j.value("work_dir", ".");
        config_.use_git = j.value("use_git", false);
        
        std::string provider_str = j.value("provider", "unknown");
        config_.provider = parseProvider(provider_str);
        
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
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    
    try {
        nlohmann::json j;
        j["api_url"] = config_.api_url;
        j["api_key"] = config_.api_key;
        j["model"] = config_.model;
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
        return true;
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
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    
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
        default: return "Unknown";
    }
}

bool ConfigManager::validate() const {
    if (config_.provider == ProviderType::Unknown) {
        return false;
    }
    
    if (config_.provider != ProviderType::LlamaCpp && config_.api_key.empty()) {
        return false;
    }
    
    if (config_.api_url.empty()) {
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

std::vector<WorkspaceHistoryEntry> ConfigManager::getRecentWorkspaces(int limit, int days) {
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

std::vector<ConversationRecord> ConfigManager::getConversations() {
    return config_.conversations;
}

bool ConfigManager::loadConversation(const std::string& file_path, std::vector<ChatMessage>& messages) {
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

bool ConfigManager::loadApiKeyFromEnv() {
    const char* key = std::getenv("OPENCODE_API_KEY");
    if (key) {
        config_.api_key = key;
        return true;
    }
    return false;
}

void ConfigManager::saveApiKeyToEnv(const std::string& api_key) {
    // 保存到.env 文件
    std::string env_file = getEnvFilePath();
    std::string content;
    
    // 读取现有内容
    std::ifstream infile(env_file);
    if (infile.is_open()) {
        std::string line;
        while (std::getline(infile, line)) {
            if (line.find("OPENCODE_API_KEY=") != 0) {
                content += line + "\n";
            }
        }
        infile.close();
    }
    
    // 添加新的 API key
    content += "OPENCODE_API_KEY=" + api_key + "\n";
    
    std::ofstream outfile(env_file);
    if (outfile.is_open()) {
        outfile << content;
        outfile.close();
    }
}

} // namespace opencode
