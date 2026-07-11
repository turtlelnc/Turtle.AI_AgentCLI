#include "config_manager.hpp"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <sys/stat.h>

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
        
        std::ofstream file(full_path);
        file << j.dump(4);
        return file.good();
    } catch (const std::exception& e) {
        std::cerr << "Error saving config: " << e.what() << std::endl;
        return false;
    }
}

std::string ConfigManager::get_api_url() const {
    if (config_.mode == ApiMode::LlamaCpp) {
        return "http://" + config_.llama_host + ":" + std::to_string(config_.llama_port) + "/v1/chat/completions";
    } else if (config_.mode == ApiMode::OpenAI) {
        return "https://api.openai.com/v1/chat/completions";
    } else if (config_.mode == ApiMode::Anthropic) {
        return "https://api.anthropic.com/v1/messages";
    }
    return "";
}

std::string ConfigManager::get_api_headers() const {
    if (config_.mode == ApiMode::LlamaCpp || config_.mode == ApiMode::OpenAI) {
        return "Authorization: Bearer " + config_.api_key;
    } else if (config_.mode == ApiMode::Anthropic) {
        return "x-api-key: " + config_.api_key;
    }
    return "";
}

} // namespace opencode
