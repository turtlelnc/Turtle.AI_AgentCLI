#ifndef CONFIG_MANAGER_HPP
#define CONFIG_MANAGER_HPP

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace opencode {

enum class ApiMode {
    OpenAI,
    Anthropic,
    LlamaCpp
};

struct Config {
    ApiMode mode{ApiMode::OpenAI};
    std::string api_key;
    std::string api_provider;
    std::string model{"gpt-4o"};
    
    std::string llama_host{"localhost"};
    int llama_port{8080};
    
    std::string work_dir;
    bool use_git{true};
    
    std::vector<std::string> mcp_configs;
};

class ConfigManager {
public:
    ConfigManager();
    
    bool load(const std::string& path = "~/.opencode/config.json");
    bool save(const std::string& path = "~/.opencode/config.json");
    
    Config& config() { return config_; }
    const Config& config() const { return config_; }
    
    std::string get_api_url() const;
    std::string get_api_headers() const;
    
private:
    Config config_;
    std::string expand_path(const std::string& path);
};

} // namespace opencode

#endif // CONFIG_MANAGER_HPP
