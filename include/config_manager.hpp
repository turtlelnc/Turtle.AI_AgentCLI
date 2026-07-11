#ifndef CONFIG_MANAGER_HPP
#define CONFIG_MANAGER_HPP

#include <string>
#include <nlohmann/json.hpp>

namespace opencode {

enum class ProviderType {
    DeepSeek,
    OpenAI,
    Anthropic,
    LlamaCpp,
    Unknown
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
    
private:
    Config config_;
    std::string config_path_;
};

} // namespace opencode

#endif // CONFIG_MANAGER_HPP
