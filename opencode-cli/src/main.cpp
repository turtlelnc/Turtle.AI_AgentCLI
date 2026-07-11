#include <iostream>
#include <string>
#include <memory>
#include <filesystem>
#include <csignal>
#include <atomic>

#include "config_manager.hpp"
#include "git_manager.hpp"
#include "mcp_manager.hpp"
#include "http_client.hpp"
#include "ui.hpp"
#include "prompt.hpp"

using namespace opencode;

// Global flag for graceful shutdown
static std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int /*signum*/) {
    g_shutdown_requested = true;
    std::cout << "\n\n⚠️  Interrupt received. Type 'quit' to exit or continue chatting.\n";
}

class OpenCodeApp {
public:
    OpenCodeApp() : ui_(), config_mgr_(), git_mgr_(), mcp_mgr_(), http_client_() {}
    
    int run() {
        // Setup signal handlers
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        
        ui_.print_header();
        
        // Setup sequence
        if (!setup()) {
            return 1;
        }
        
        // Main chat loop
        return chat_loop();
    }
    
private:
    Ui ui_;
    ConfigManager config_mgr_;
    GitManager git_mgr_;
    McpManager mcp_mgr_;
    HttpClient http_client_;
    PromptManager prompt_mgr_;
    
    bool setup() {
        ui_.print_welcome();
        
        // Step 1: Choose API mode
        std::vector<std::string> mode_options = {
            "LiteLLM Proxy (unified interface)",
            "OpenAI API",
            "Anthropic API",
            "Google AI / Vertex",
            "Groq",
            "TogetherAI",
            "OpenRouter",
            "Llama.cpp (local)",
            "Custom OpenAI-compatible endpoint"
        };
        int mode_choice = ui_.show_menu("Select AI Provider", mode_options);
        if (mode_choice < 0) mode_choice = 0;
        
        config_mgr_.config().mode = static_cast<ApiMode>(mode_choice);
        
        // Step 2: Configure credentials or local settings
        if (config_mgr_.config().mode == ApiMode::LiteLLM_Proxy) {
            std::cout << "\n📦 LiteLLM Proxy Mode - Unified interface for 100+ LLM providers\n";
            std::string host = ui_.get_text_input("LiteLLM server host (default: localhost)");
            if (!host.empty()) {
                config_mgr_.config().litellm_host = host;
            }
            
            std::string port_str = ui_.get_text_input("LiteLLM server port (default: 4000)");
            if (!port_str.empty()) {
                try {
                    config_mgr_.config().litellm_port = std::stoi(port_str);
                } catch (...) {
                    ui_.print_error("Invalid port, using default 4000");
                }
            }
            
            std::string base_url = ui_.get_text_input("Custom base URL (optional, press Enter to skip)");
            if (!base_url.empty()) {
                config_mgr_.config().litellm_base_url = base_url;
            }
            
            std::string api_key = ui_.get_text_input("API key (optional for local LiteLLM)");
            config_mgr_.config().api_key = api_key;
            
            // Auto-detect provider from LiteLLM
            std::string litellm_url = config_mgr_.build_litellm_url();
            ProviderType detected = config_mgr_.detect_provider(litellm_url);
            if (detected != ProviderType::Unknown) {
                std::cout << "✓ Auto-detected provider: " << config_mgr_.provider_type_to_string(detected) << "\n";
                config_mgr_.config().api_provider = config_mgr_.provider_type_to_string(detected);
            }
            
            config_mgr_.fetch_models_from_litellm();
            
        } else if (config_mgr_.config().mode == ApiMode::LlamaCpp) {
            std::string host = ui_.get_text_input("Llama.cpp server host (default: localhost)");
            if (!host.empty()) {
                config_mgr_.config().llama_host = host;
            }
            
            std::string port_str = ui_.get_text_input("Llama.cpp server port (default: 8080)");
            if (!port_str.empty()) {
                try {
                    config_mgr_.config().llama_port = std::stoi(port_str);
                } catch (...) {
                    ui_.print_error("Invalid port, using default 8080");
                }
            }
            
        } else if (config_mgr_.config().mode == ApiMode::OpenRouter || 
                   !config_mgr_.config().custom_base_url.empty()) {
            // Custom endpoint configuration
            if (config_mgr_.config().mode != ApiMode::OpenRouter) {
                std::string custom_url = ui_.get_text_input("Custom API base URL");
                config_mgr_.config().custom_base_url = custom_url;
            } else {
                config_mgr_.config().custom_base_url = "https://openrouter.ai/api/v1";
            }
            std::string api_key = ui_.get_text_input("Enter your API key");
            config_mgr_.config().api_key = api_key;
            
        } else {
            // Standard providers (OpenAI, Anthropic, Google, Groq, TogetherAI)
            std::string api_key = ui_.get_text_input("Enter your API key");
            config_mgr_.config().api_key = api_key;
            
            // Set provider-specific defaults
            switch (config_mgr_.config().mode) {
                case ApiMode::Anthropic:
                    config_mgr_.config().api_provider = "anthropic";
                    config_mgr_.config().model = "claude-sonnet-4-20250514";
                    break;
                case ApiMode::Google:
                    config_mgr_.config().api_provider = "google";
                    config_mgr_.config().model = "gemini-2.0-flash-exp";
                    break;
                case ApiMode::Groq:
                    config_mgr_.config().api_provider = "groq";
                    config_mgr_.config().model = "llama-3.3-70b-versatile";
                    break;
                case ApiMode::TogetherAI:
                    config_mgr_.config().api_provider = "together";
                    config_mgr_.config().model = "meta-llama/Llama-3.3-70B-Instruct-Turbo";
                    break;
                case ApiMode::OpenRouter:
                    config_mgr_.config().api_provider = "openrouter";
                    config_mgr_.config().model = "meta-llama/llama-3.3-70b-instruct";
                    break;
                default:  // OpenAI
                    config_mgr_.config().api_provider = "openai";
                    config_mgr_.config().model = "gpt-4o";
            }
        }
        
        // Request settings
        std::string max_tok = ui_.get_text_input("Max tokens (default: 4096)");
        if (!max_tok.empty()) {
            try { config_mgr_.config().max_tokens = std::stoi(max_tok); } catch (...) {}
        }
        std::string temp = ui_.get_text_input("Temperature (default: 0.7)");
        if (!temp.empty()) {
            try { config_mgr_.config().temperature = std::stod(temp); } catch (...) {}
        }
        
        // Step 3: Select working directory
        std::string work_dir = ui_.get_text_input("Working directory (default: current)");
        if (work_dir.empty()) {
            work_dir = std::filesystem::current_path().string();
        }
        config_mgr_.config().work_dir = work_dir;
        
        // Step 4: Git setup
        std::vector<std::string> git_options = {"Use Git", "No Git"};
        int git_choice = ui_.show_menu("Git Integration", git_options);
        config_mgr_.config().use_git = (git_choice == 0);
        
        if (config_mgr_.config().use_git) {
            GitStatus status = git_mgr_.check_status(work_dir);
            if (!status.is_repo) {
                std::cout << "\nDirectory is not a Git repository.\n";
                std::string init = ui_.get_user_input("Initialize Git repo? (y/n)");
                if (init == "y" || init == "Y") {
                    git_mgr_.init_repo(work_dir);
                    std::cout << "Git repository initialized.\n";
                }
            } else if (status.has_uncommitted) {
                std::cout << "\n⚠️  Uncommitted changes detected:\n";
                for (const auto& f : status.modified_files) {
                    std::cout << "   M " << f << "\n";
                }
                for (const auto& f : status.untracked_files) {
                    std::cout << "   ? " << f << "\n";
                }
            }
            
            if (status.behind_remote) {
                std::cout << "\n⚠️  Local branch is behind remote by " 
                          << status.commits_behind << " commit(s).\n";
                std::string pull = ui_.get_user_input("Pull remote changes? (y/n)");
                if (pull == "y" || pull == "Y") {
                    git_mgr_.pull_remote(work_dir);
                }
            }
        }
        
        // Save configuration
        config_mgr_.save();
        
        // Validate configuration
        if (!config_mgr_.validate()) {
            ui_.print_error("Invalid configuration. Please check your settings.");
            return false;
        }
        
        std::cout << "\n✓ Setup complete. Starting chat session...\n";
        return true;
    }
    
    int chat_loop() {
        ui_.print_welcome();
        
        while (true) {
            std::string input = ui_.get_user_input("Message");
            
            if (input.empty()) continue;
            
            if (input == "quit" || input == "exit" || input == "/q") {
                std::cout << "\nGoodbye!\n";
                break;
            }
            
            if (input == "help" || input == "/h") {
                show_help();
                continue;
            }
            
            if (input == "clear" || input == "/c") {
                prompt_mgr_.clear();
                ui_.clear_screen();
                ui_.print_header();
                continue;
            }
            
            if (input == "tools" || input == "/t") {
                auto tools = mcp_mgr_.list_tools();
                std::cout << "\nAvailable MCP Tools:\n";
                for (const auto& t : tools) {
                    std::cout << "  • " << t << "\n";
                }
                continue;
            }
            
            if (input == "status" || input == "/s") {
                show_status();
                continue;
            }
            
            // Add user message to context
            prompt_mgr_.add_message("user", input);
            ui_.print_message(input, "user");
            
            // Build request
            nlohmann::json request_body;
            std::string url = config_mgr_.get_api_url();
            std::string headers = config_mgr_.get_api_headers();
            
            if (config_mgr_.config().mode == ApiMode::Anthropic) {
                request_body["model"] = config_mgr_.config().model;
                request_body["max_tokens"] = 4096;
                request_body["messages"] = prompt_mgr_.to_anthropic_format();
            } else {
                request_body["model"] = config_mgr_.config().model;
                request_body["messages"] = prompt_mgr_.to_openai_format();
                request_body["max_tokens"] = 4096;
            }
            
            // Send request
            std::cout << "\n🤔 Thinking...\n";
            auto response = http_client_.post(url, request_body, headers);
            
            if (!response.success()) {
                ui_.print_error("API request failed: " + std::to_string(response.status_code));
                std::cout << response.body << "\n";
                continue;
            }
            
            // Parse response
            try {
                nlohmann::json resp_json = nlohmann::json::parse(response.body);
                std::string assistant_message;
                
                if (config_mgr_.config().mode == ApiMode::Anthropic) {
                    if (resp_json.contains("content") && resp_json["content"].is_array()) {
                        for (const auto& c : resp_json["content"]) {
                            if (c.contains("text")) {
                                assistant_message += c["text"].get<std::string>();
                            }
                        }
                    }
                } else {
                    if (resp_json.contains("choices") && resp_json["choices"].is_array()) {
                        assistant_message = resp_json["choices"][0]["message"]["content"].get<std::string>();
                    }
                }
                
                if (!assistant_message.empty()) {
                    ui_.print_message(assistant_message, "assistant");
                    prompt_mgr_.add_message("assistant", assistant_message);
                }
            } catch (const std::exception& e) {
                ui_.print_error(std::string("Failed to parse response: ") + e.what());
            }
        }
        
        return 0;
    }
    
    void show_help() {
        std::cout << "\n┌─ Available Commands\n";
        std::cout << "│ /q, quit, exit  - Exit the application\n";
        std::cout << "│ /h, help        - Show this help message\n";
        std::cout << "│ /c, clear       - Clear conversation history\n";
        std::cout << "│ /t, tools       - List available MCP tools\n";
        std::cout << "│ /s, status      - Show current configuration\n";
        std::cout << "│\n";
        std::cout << "│ Just type your message to chat with the AI.\n";
        std::cout << "└─\n\n";
    }
    
    void show_status() {
        std::cout << "\n┌─ Current Configuration\n";
        std::cout << "│ Mode: ";
        switch (config_mgr_.config().mode) {
            case ApiMode::LiteLLM_Proxy: std::cout << "LiteLLM Proxy"; break;
            case ApiMode::OpenAI: std::cout << "OpenAI"; break;
            case ApiMode::Anthropic: std::cout << "Anthropic"; break;
            case ApiMode::Google: std::cout << "Google AI/Vertex"; break;
            case ApiMode::Groq: std::cout << "Groq"; break;
            case ApiMode::TogetherAI: std::cout << "TogetherAI"; break;
            case ApiMode::OpenRouter: std::cout << "OpenRouter"; break;
            case ApiMode::LlamaCpp: std::cout << "Llama.cpp (local)"; break;
        }
        std::cout << "\n│ Model: " << config_mgr_.config().model << "\n";
        if (!config_mgr_.config().api_provider.empty()) {
            std::cout << "│ Provider: " << config_mgr_.config().api_provider << "\n";
        }
        if (config_mgr_.config().mode == ApiMode::LiteLLM_Proxy) {
            std::string url = config_mgr_.build_litellm_url();
            std::cout << "│ LiteLLM URL: " << url << "\n";
        } else if (config_mgr_.config().mode == ApiMode::LlamaCpp) {
            std::cout << "│ Server: " << config_mgr_.config().llama_host 
                      << ":" << config_mgr_.config().llama_port << "\n";
        } else if (!config_mgr_.config().custom_base_url.empty()) {
            std::cout << "│ Custom URL: " << config_mgr_.config().custom_base_url << "\n";
        }
        std::cout << "│ Work Dir: " << config_mgr_.config().work_dir << "\n";
        std::cout << "│ Git: " << (config_mgr_.config().use_git ? "Enabled" : "Disabled") << "\n";
        std::cout << "│ Max Tokens: " << config_mgr_.config().max_tokens << "\n";
        std::cout << "│ Temperature: " << config_mgr_.config().temperature << "\n";
        std::cout << "└─\n\n";
    }
};

int main(int /*argc*/, char* /*argv*/[]) {
    OpenCodeApp app;
    return app.run();
}
