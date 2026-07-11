#include "config_manager.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>

namespace opencode {

ConfigManager::ConfigManager() {
    config_.provider = ProviderType::Unknown;
    config_.total_input_tokens = 0;
    config_.total_output_tokens = 0;
    config_.total_cost_usd = 0.0;
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

} // namespace opencode
