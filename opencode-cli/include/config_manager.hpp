#ifndef CONFIG_MANAGER_HPP
#define CONFIG_MANAGER_HPP

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace opencode {

enum class ApiMode {
    OpenAI,
    Anthropic,
    Google,
    Groq,
    TogetherAI,
    OpenRouter,
    LlamaCpp,
    LiteLLM_Proxy  // LiteLLM unified proxy mode
};

// Model provider auto-detection result
enum class ProviderType {
    Unknown,
    OpenAI,
    Anthropic,
    Google,
    Groq,
    TogetherAI,
    Mistral,
    Cohere,
    HuggingFace,
    Ollama,
    LlamaCpp
};

struct ModelInfo {
    std::string id;
    std::string name;
    std::string provider;
    double input_cost_per_token{0.0};
    double output_cost_per_token{0.0};
    int max_tokens{4096};
    bool supports_vision{false};
    bool supports_function_calling{true};
};

struct Config {
    ApiMode mode{ApiMode::LiteLLM_Proxy};
    std::string api_key;
    std::string api_provider;  // Explicit provider override
    std::string model{"gpt-4o"};
    
    // LiteLLM proxy settings
    std::string litellm_host{"localhost"};
    int litellm_port{4000};
    std::string litellm_base_url{""};  // Full URL override
    
    // Llama.cpp local settings
    std::string llama_host{"localhost"};
    int llama_port{8080};
    
    // Custom endpoint for OpenAI-compatible APIs
    std::string custom_base_url{""};
    
    std::string work_dir;
    bool use_git{true};
    
    // Request settings
    int max_tokens{4096};
    double temperature{0.7};
    double top_p{1.0};
    
    std::vector<std::string> mcp_configs;
    
    // Auto-detected provider
    ProviderType detected_provider{ProviderType::Unknown};
    std::vector<ModelInfo> available_models;
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
    bool validate() const;  // Validate configuration
    
    // LiteLLM integration methods
    ProviderType detect_provider(const std::string& base_url);
    bool fetch_models_from_litellm();
    std::string provider_type_to_string(ProviderType type) const;
    ApiMode provider_type_to_mode(ProviderType type) const;
    std::string build_litellm_url() const;  // Made public for main.cpp access
    
private:
    Config config_;
    std::string expand_path(const std::string& path);
};

} // namespace opencode

#endif // CONFIG_MANAGER_HPP
