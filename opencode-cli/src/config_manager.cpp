#include "config_manager.hpp"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <sys/stat.h>
#include <algorithm>

namespace opencode {

ConfigManager::ConfigManager() {}

std::string ConfigManager::expand_path(const std::string& path) {
    std::string result = path;
    if (!result.empty() && result[0] == '~') {
        const char* home = std::getenv("HOME");
        if (home) {
            result = std::string(home) + result.substr(1);
        }
    }
    return result;
}

std::string ConfigManager::build_litellm_url() const {
    if (!config_.litellm_base_url.empty()) {
        return config_.litellm_base_url;
    }
    return "http://" + config_.litellm_host + ":" + std::to_string(config_.litellm_port) + "/v1";
}

ProviderType ConfigManager::detect_provider(const std::string& base_url) {
    // Auto-detect provider based on URL patterns
    std::string url_lower = base_url;
    std::transform(url_lower.begin(), url_lower.end(), url_lower.begin(), ::tolower);
    
    if (url_lower.find("openai") != std::string::npos) return ProviderType::OpenAI;
    if (url_lower.find("anthropic") != std::string::npos) return ProviderType::Anthropic;
    if (url_lower.find("google") != std::string::npos || url_lower.find("vertex") != std::string::npos) return ProviderType::Google;
    if (url_lower.find("groq") != std::string::npos) return ProviderType::Groq;
    if (url_lower.find("together") != std::string::npos) return ProviderType::TogetherAI;
    if (url_lower.find("mistral") != std::string::npos) return ProviderType::Mistral;
    if (url_lower.find("cohere") != std::string::npos) return ProviderType::Cohere;
    if (url_lower.find("huggingface") != std::string::npos || url_lower.find("hfface") != std::string::npos) return ProviderType::HuggingFace;
    if (url_lower.find("ollama") != std::string::npos) return ProviderType::Ollama;
    if (url_lower.find("llama") != std::string::npos || url_lower.find("llamacpp") != std::string::npos) return ProviderType::LlamaCpp;
    
    return ProviderType::Unknown;
}

bool ConfigManager::fetch_models_from_litellm() {
    // This would require HTTP client integration - for now, return true as placeholder
    // In production, this would call LiteLLM's /v1/models endpoint
    std::cout << "ℹ️  LiteLLM proxy mode: Model discovery available at runtime\n";
    config_.detected_provider = ProviderType::Unknown;  // Will be detected on first request
    return true;
}

std::string ConfigManager::provider_type_to_string(ProviderType type) const {
    switch (type) {
        case ProviderType::OpenAI: return "OpenAI";
        case ProviderType::Anthropic: return "Anthropic";
        case ProviderType::Google: return "Google";
        case ProviderType::Groq: return "Groq";
        case ProviderType::TogetherAI: return "TogetherAI";
        case ProviderType::Mistral: return "Mistral";
        case ProviderType::Cohere: return "Cohere";
        case ProviderType::HuggingFace: return "HuggingFace";
        case ProviderType::Ollama: return "Ollama";
        case ProviderType::LlamaCpp: return "LlamaCpp";
        default: return "Unknown";
    }
}

ApiMode ConfigManager::provider_type_to_mode(ProviderType type) const {
    switch (type) {
        case ProviderType::OpenAI: return ApiMode::OpenAI;
        case ProviderType::Anthropic: return ApiMode::Anthropic;
        case ProviderType::Google: return ApiMode::Google;
        case ProviderType::Groq: return ApiMode::Groq;
        case ProviderType::TogetherAI: return ApiMode::TogetherAI;
        case ProviderType::LlamaCpp: 
        case ProviderType::Ollama: return ApiMode::LlamaCpp;
        default: return ApiMode::LiteLLM_Proxy;
    }
}

bool ConfigManager::load(const std::string& path) {
    std::string full_path = expand_path(path);
    std::ifstream file(full_path);
    if (!file.is_open()) {
        return false;
    }
    
    try {
        nlohmann::json j;
        file >> j;
        
        if (j.contains("mode")) {
            config_.mode = static_cast<ApiMode>(j["mode"].get<int>());
        }
        if (j.contains("api_key")) config_.api_key = j["api_key"].get<std::string>();
        if (j.contains("api_provider")) config_.api_provider = j["api_provider"].get<std::string>();
        if (j.contains("model")) config_.model = j["model"].get<std::string>();
        if (j.contains("llama_host")) config_.llama_host = j["llama_host"].get<std::string>();
        if (j.contains("llama_port")) config_.llama_port = j["llama_port"].get<int>();
        if (j.contains("work_dir")) config_.work_dir = j["work_dir"].get<std::string>();
        if (j.contains("use_git")) config_.use_git = j["use_git"].get<bool>();
        if (j.contains("mcp_configs")) {
            config_.mcp_configs = j["mcp_configs"].get<std::vector<std::string>>();
        }
        // LiteLLM settings
        if (j.contains("litellm_host")) config_.litellm_host = j["litellm_host"].get<std::string>();
        if (j.contains("litellm_port")) config_.litellm_port = j["litellm_port"].get<int>();
        if (j.contains("litellm_base_url")) config_.litellm_base_url = j["litellm_base_url"].get<std::string>();
        if (j.contains("custom_base_url")) config_.custom_base_url = j["custom_base_url"].get<std::string>();
        if (j.contains("max_tokens")) config_.max_tokens = j["max_tokens"].get<int>();
        if (j.contains("temperature")) config_.temperature = j["temperature"].get<double>();
        if (j.contains("top_p")) config_.top_p = j["top_p"].get<double>();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading config: " << e.what() << std::endl;
        return false;
    }
}

bool ConfigManager::save(const std::string& path) {
    std::string full_path = expand_path(path);
    
    size_t last_slash = full_path.rfind('/');
    if (last_slash != std::string::npos) {
        std::string dir = full_path.substr(0, last_slash);
        mkdir(dir.c_str(), 0755);
    }
    
    try {
        nlohmann::json j;
        j["mode"] = static_cast<int>(config_.mode);
        j["api_key"] = config_.api_key;
        j["api_provider"] = config_.api_provider;
        j["model"] = config_.model;
        j["llama_host"] = config_.llama_host;
        j["llama_port"] = config_.llama_port;
        j["work_dir"] = config_.work_dir;
        j["use_git"] = config_.use_git;
        j["mcp_configs"] = config_.mcp_configs;
        // LiteLLM settings
        j["litellm_host"] = config_.litellm_host;
        j["litellm_port"] = config_.litellm_port;
        j["litellm_base_url"] = config_.litellm_base_url;
        j["custom_base_url"] = config_.custom_base_url;
        j["max_tokens"] = config_.max_tokens;
        j["temperature"] = config_.temperature;
        j["top_p"] = config_.top_p;
        
        std::ofstream file(full_path);
        file << j.dump(4);
        return file.good();
    } catch (const std::exception& e) {
        std::cerr << "Error saving config: " << e.what() << std::endl;
        return false;
    }
}

std::string ConfigManager::get_api_url() const {
    if (config_.mode == ApiMode::LiteLLM_Proxy) {
        return build_litellm_url() + "/chat/completions";
    } else if (config_.mode == ApiMode::LlamaCpp) {
        return "http://" + config_.llama_host + ":" + std::to_string(config_.llama_port) + "/v1/chat/completions";
    } else if (config_.mode == ApiMode::OpenAI) {
        return "https://api.openai.com/v1/chat/completions";
    } else if (config_.mode == ApiMode::Anthropic) {
        return "https://api.anthropic.com/v1/messages";
    } else if (config_.mode == ApiMode::Google) {
        return "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions";
    } else if (config_.mode == ApiMode::Groq) {
        return "https://api.groq.com/openai/v1/chat/completions";
    } else if (config_.mode == ApiMode::TogetherAI) {
        return "https://api.together.xyz/v1/chat/completions";
    } else if (config_.mode == ApiMode::OpenRouter) {
        return "https://openrouter.ai/api/v1/chat/completions";
    } else if (!config_.custom_base_url.empty()) {
        // Custom OpenAI-compatible endpoint
        if (config_.custom_base_url.back() == '/') {
            return config_.custom_base_url + "chat/completions";
        }
        return config_.custom_base_url + "/chat/completions";
    }
    return "";
}

std::string ConfigManager::get_api_headers() const {
    std::string headers;
    
    if (config_.mode == ApiMode::LiteLLM_Proxy || 
        config_.mode == ApiMode::LlamaCpp || 
        config_.mode == ApiMode::OpenAI ||
        config_.mode == ApiMode::Google ||
        config_.mode == ApiMode::Groq ||
        config_.mode == ApiMode::TogetherAI ||
        config_.mode == ApiMode::OpenRouter ||
        !config_.custom_base_url.empty()) {
        // OpenAI-compatible format
        headers = "Authorization: Bearer " + config_.api_key;
        if (!config_.api_provider.empty()) {
            headers += "\nX-Provider: " + config_.api_provider;
        }
    } else if (config_.mode == ApiMode::Anthropic) {
        headers = "x-api-key: " + config_.api_key + "\nx-anthropic-version: 2023-06-01";
    }
    
    return headers;
}

bool ConfigManager::validate() const {
    if (config_.mode == ApiMode::LiteLLM_Proxy) {
        // LiteLLM proxy may not require API key if running locally
        return !config_.litellm_host.empty() && config_.litellm_port > 0 && config_.litellm_port < 65536;
    } else if (config_.mode == ApiMode::LlamaCpp) {
        return !config_.llama_host.empty() && config_.llama_port > 0 && config_.llama_port < 65536;
    } else if (!config_.custom_base_url.empty()) {
        // Custom endpoint - API key optional depending on server
        return true;
    } else {
        return !config_.api_key.empty();
    }
}

} // namespace opencode
