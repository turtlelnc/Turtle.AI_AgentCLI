#ifndef CONFIG_MANAGER_HPP
#define CONFIG_MANAGER_HPP

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace opencode {

// Forward declaration
struct ChatMessage;

enum class ProviderType {
    DeepSeek,
    OpenAI,
    Anthropic,
    LlamaCpp,
    OpenAICompatible,
    Unknown
};

struct WorkspaceHistoryEntry {
    std::string path;
    int64_t last_used;  // Unix timestamp
};

struct ConversationRecord {
    std::string name;
    std::string file_path;
    int64_t created_at;
    int64_t updated_at;
};

struct Config {
    ProviderType provider;
    std::string api_url;
    std::string api_key;
    std::string model;
    bool subagent_enabled = false;
    ProviderType subagent_provider = ProviderType::Unknown;
    std::string subagent_api_url;
    std::string subagent_model;
    std::string work_dir;
    bool use_git;
    
    // Token 追踪
    int64_t total_input_tokens;
    int64_t total_output_tokens;
    double total_cost_usd;
    
    // 历史记录
    std::vector<WorkspaceHistoryEntry> workspace_history;
    std::vector<ConversationRecord> conversations;
};

class ConfigManager {
public:
    ConfigManager();
    
    bool loadConfig(const std::string& path);
    bool saveConfig(const std::string& path);
    bool loadDefaultConfig();
    std::string getDefaultConfigPath() const;
    std::optional<std::string> takeLegacyApiKey(ProviderType provider);
    bool hasLegacyApiKey() const;
    bool sanitizeLegacyCredentials();
    
    Config& getConfig();
    const Config& getConfig() const;
    
    static ProviderType parseProvider(const std::string& name);
    static std::string providerToString(ProviderType p);
    
    // 验证配置
    bool validate() const;
    
    // 管理工作区历史
    void addWorkspaceToHistory(const std::string& path);
    std::vector<WorkspaceHistoryEntry> getRecentWorkspaces(int limit = 15, int days = 30) const;

    // 管理对话记录
    void addConversation(const ConversationRecord& record);
    std::vector<ConversationRecord> getConversations() const;
    bool loadConversation(const std::string& file_path, std::vector<ChatMessage>& messages) const;
    bool saveConversation(const std::string& file_path, const std::vector<ChatMessage>& messages);
    
    // Display only; credentials are managed by SecretStore.
    std::string maskApiKey(const std::string& api_key);

    // ─── Named configuration profiles ────────────────────────────────
    bool setProfile(const std::string& name);
    std::string activeProfile() const { return active_profile_; }
    std::vector<std::string> listProfiles() const;

private:
    Config config_;
    std::string config_path_;
    std::string active_profile_ = "default";
    std::optional<std::string> legacy_api_key_;
    ProviderType legacy_key_provider_ = ProviderType::Unknown;
    std::vector<std::string> legacy_credential_files_;
    static std::string homeDirectory();
};

} // namespace opencode

#endif // CONFIG_MANAGER_HPP
