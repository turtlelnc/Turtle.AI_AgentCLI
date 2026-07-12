#ifndef CONFIG_MANAGER_HPP
#define CONFIG_MANAGER_HPP

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace opencode {

// Forward declaration
struct ChatMessage;

enum class ProviderType {
    DeepSeek,
    OpenAI,
    Anthropic,
    LlamaCpp,
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
    
    Config& getConfig();
    const Config& getConfig() const;
    
    static ProviderType parseProvider(const std::string& name);
    static std::string providerToString(ProviderType p);
    
    // 验证配置
    bool validate() const;
    
    // 管理工作区历史
    void addWorkspaceToHistory(const std::string& path);
    std::vector<WorkspaceHistoryEntry> getRecentWorkspaces(int limit = 15, int days = 30);
    
    // 管理对话记录
    void addConversation(const ConversationRecord& record);
    std::vector<ConversationRecord> getConversations();
    bool loadConversation(const std::string& file_path, std::vector<ChatMessage>& messages);
    bool saveConversation(const std::string& file_path, const std::vector<ChatMessage>& messages);
    
    // API Key 管理
    std::string maskApiKey(const std::string& api_key);
    bool loadApiKeyFromEnv();
    void saveApiKeyToEnv(const std::string& api_key);
    
private:
    Config config_;
    std::string config_path_;
    std::string getGlobalConfigPath();
    std::string getEnvFilePath();
};

} // namespace opencode

#endif // CONFIG_MANAGER_HPP
