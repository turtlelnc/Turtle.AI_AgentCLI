#include "ui.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <clocale>
#include <codecvt>
#include <locale>

namespace opencode {

UI::UI() {
    // 设置 UTF-8 locale
    std::setlocale(LC_ALL, "en_US.UTF-8");
}

std::string UI::fixUtf8Encoding(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        
        // 检查是否为有效的 UTF-8 起始字节
        if (c < 0x80) {
            // ASCII
            result += static_cast<char>(c);
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte sequence
            if (i + 1 < input.size() && (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80) {
                result += input[i];
                result += input[i+1];
                ++i;
            } else {
                // Invalid, skip
                result += '?';
            }
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte sequence
            if (i + 2 < input.size() && 
                (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(input[i+2]) & 0xC0) == 0x80) {
                result += input[i];
                result += input[i+1];
                result += input[i+2];
                i += 2;
            } else {
                result += '?';
            }
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte sequence
            if (i + 3 < input.size() && 
                (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(input[i+2]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(input[i+3]) & 0xC0) == 0x80) {
                result += input[i];
                result += input[i+1];
                result += input[i+2];
                result += input[i+3];
                i += 3;
            } else {
                result += '?';
            }
        } else {
            // Invalid UTF-8 byte
            result += '?';
        }
    }
    
    return result;
}

void UI::printBanner() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║                                                           ║\n";
    std::cout << "║   ███╗   ██╗ ██████╗ ██╗   ██╗ █████╗  ██████╗███████╗     ║\n";
    std::cout << "║   ████╗  ██║██╔═══██╗██║   ██║██╔══██╗██╔════╝██╔════╝     ║\n";
    std::cout << "║   ██╔██╗ ██║██║   ██║██║   ██║███████║██║     █████╗       ║\n";
    std::cout << "║   ██║╚██╗██║██║   ██║╚██╗ ██╔╝██╔══██║██║     ██╔══╝       ║\n";
    std::cout << "║   ██║ ╚████║╚██████╔╝ ╚████╔╝ ██║  ██║╚██████╗███████╗     ║\n";
    std::cout << "║   ╚═╝  ╚═══╝ ╚═════╝   ╚═══╝  ╚═╝  ╚═╝ ╚═════╝╚══════╝     ║\n";
    std::cout << "║                                                           ║\n";
    std::cout << "║              AI-Powered CLI Assistant                     ║\n";
    std::cout << "║              Powered by LiteLLM Compatible APIs           ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
}

void UI::showWelcome() {
    clearScreen();
    printBanner();
    std::cout << " Welcome to OpenCode CLI - Your intelligent coding assistant!\n";
    std::cout << "\n";
    std::cout << " Features:\n";
    std::cout << "  • Multi-provider support (DeepSeek, OpenAI, Anthropic, LlamaCpp)\n";
    std::cout << "  • Built-in MCP tools for file operations\n";
    std::cout << "  • Git integration with safety checks\n";
    std::cout << "  • Real-time token usage and cost tracking\n";
    std::cout << "\n";
}

int UI::showConfigWizard() {
    std::cout << "\n┌─────────────────────────────────────────────────────┐\n";
    std::cout << "│  Configuration Wizard                                 │\n";
    std::cout << "└─────────────────────────────────────────────────────┘\n\n";
    
    std::cout << "Select API Provider:\n";
    std::cout << "  1. DeepSeek (https://api.deepseek.com)\n";
    std::cout << "  2. OpenAI (https://api.openai.com)\n";
    std::cout << "  3. Anthropic (https://api.anthropic.com)\n";
    std::cout << "  4. LlamaCpp/Ollama (Local)\n";
    std::cout << "\n";
    
    int choice;
    std::cout << "Enter choice [1-4]: ";
    std::cin >> choice;
    return choice;
}

std::string UI::getInput(const std::string& prompt) {
    std::cout << "\n┌─[ " << prompt << " ]\n";
    std::cout << "└─► ";
    
    std::string input;
    std::getline(std::cin >> std::ws, input);
    return input;
}

void UI::showMessage(const std::string& role, const std::string& content) {
    std::cout << "\n┌─[ " << role << " ]\n";
    std::cout << "└─► " << content << "\n";
}

void UI::showAIResponse(const std::string& content) {
    std::cout << "\n╭─────────────────────────────────────────────────────╮\n";
    std::cout << "│  🤖 AI Response                                   │\n";
    std::cout << "╰─────────────────────────────────────────────────────╯\n";
    std::cout << "\n";
    
    std::string fixed_content = fixUtf8Encoding(content);
    // 简单换行处理
    size_t pos = 0;
    size_t line_len = 70;
    while (pos < fixed_content.length()) {
        size_t end = fixed_content.find('\n', pos);
        if (end == std::string::npos || end - pos > line_len) {
            end = pos + line_len;
            if (end > fixed_content.length()) end = fixed_content.length();
        }
        std::cout << "  " << fixed_content.substr(pos, end - pos) << "\n";
        pos = end + 1;
    }
    std::cout << "\n";
}

void UI::showTokenStats(int64_t input_tokens, int64_t output_tokens, double cost_usd) {
    std::cout << "\n┌─────────────────────────────────────────────────────┐\n";
    std::cout << "│  💰 Token Usage & Cost                              │\n";
    std::cout << "├─────────────────────────────────────────────────────┤\n";
    std::cout << "│  Input Tokens:  " << std::setw(10) << input_tokens << "                   │\n";
    std::cout << "│  Output Tokens: " << std::setw(10) << output_tokens << "                   │\n";
    std::cout << "│  ─────────────────────────────────────────────────  │\n";
    std::cout << "│  Total Cost:    $" << std::fixed << std::setprecision(6) << std::setw(8) << cost_usd << " USD               │\n";
    std::cout << "└─────────────────────────────────────────────────────┘\n";
}

void UI::showError(const std::string& message) {
    std::cout << "\n╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ❌ ERROR                                                ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════════╣\n";
    std::cout << "║  " << std::left << std::setw(58) << message.substr(0, 58) << " ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n";
}

void UI::clearScreen() {
    std::cout << "\033[2J\033[H";
    std::cout.flush();
}

std::string UI::getUtf8Border() {
    return "═";
}

void UI::showHistory(const std::vector<std::pair<std::string, std::string>>& history) {
    std::cout << "\n┌─────────────────────────────────────────────────────┐\n";
    std::cout << "│  Conversation History                               │\n";
    std::cout << "└─────────────────────────────────────────────────────┘\n";
    
    for (const auto& [role, content] : history) {
        std::cout << "\n[" << role << "]: " << content.substr(0, 100);
        if (content.length() > 100) std::cout << "...";
        std::cout << "\n";
    }
}

} // namespace opencode

std::string UI::showWorkspaceSelection(ConfigManager& config_mgr) {
    auto recent = config_mgr.getRecentWorkspaces(15, 30);
    
    std::cout << "\n┌─────────────────────────────────────────────────────┐\n";
    std::cout << "│  Select Working Directory                             │\n";
    std::cout << "└─────────────────────────────────────────────────────┘\n\n";
    
    if (!recent.empty()) {
        std::cout << "Recent workspaces (last 30 days):\n";
        int idx = 1;
        for (const auto& entry : recent) {
            std::cout << "  " << idx << ". " << entry.path << "\n";
            idx++;
        }
        std::cout << "\n";
        
        std::cout << "  " << (idx) << ". Enter new workspace path\n";
        std::cout << "\n";
        
        std::cout << "Enter choice [" << 1 << "-" << idx << "]: ";
        int choice;
        std::cin >> choice;
        std::cin.ignore();
        
        if (choice >= 1 && choice <= static_cast<int>(recent.size())) {
            return recent[choice - 1].path;
        }
    }
    
    // New workspace
    std::cout << "\nEnter workspace path: ";
    std::string path;
    std::getline(std::cin >> std::ws, path);
    return path;
}

int UI::showConversationSelection(ConfigManager& config_mgr, const std::string& work_dir) {
    auto conversations = config_mgr.getConversations();
    
    std::cout << "\n┌─────────────────────────────────────────────────────┐\n";
    std::cout << "│  Conversation Options                                 │\n";
    std::cout << "└─────────────────────────────────────────────────────┘\n\n";
    
    if (!conversations.empty()) {
        std::cout << "Available conversations:\n";
        int idx = 1;
        for (const auto& conv : conversations) {
            std::cout << "  " << idx << ". " << conv.name << " (" << conv.file_path << ")\n";
            idx++;
        }
        std::cout << "\n";
        
        std::cout << "  " << idx << ". Start new conversation\n";
        std::cout << "\n";
        
        std::cout << "Enter choice [" << 1 << "-" << idx << "]: ";
        int choice;
        std::cin >> choice;
        std::cin.ignore();
        
        if (choice >= 1 && choice < idx) {
            return choice - 1;  // Return index of selected conversation
        }
        return -1;  // New conversation
    }
    
    std::cout << "No previous conversations found.\n";
    std::cout << "Starting new conversation...\n";
    return -1;
}

std::string UI::getConversationName() {
    std::cout << "\nEnter conversation name (for saving): ";
    std::string name;
    std::getline(std::cin >> std::ws, name);
    return name;
}

std::string UI::showModelSelection(ProviderType provider, const std::string& current_model) {
    std::cout << "\n┌─────────────────────────────────────────────────────┐\n";
    std::cout << "│  Select Model                                         │\n";
    std::cout << "└─────────────────────────────────────────────────────┘\n\n";
    
    std::vector<std::string> models;
    switch (provider) {
        case ProviderType::DeepSeek:
            models = {"deepseek-chat", "deepseek-coder", "deepseek-reasoner"};
            break;
        case ProviderType::OpenAI:
            models = {"gpt-4o-mini", "gpt-4o", "gpt-4-turbo", "gpt-3.5-turbo"};
            break;
        case ProviderType::Anthropic:
            models = {"claude-3-5-sonnet-20241022", "claude-3-opus-20240229", "claude-3-haiku-20240307"};
            break;
        case ProviderType::LlamaCpp:
            models = {"llama3", "mistral", "codellama"};
            break;
        default:
            models = {current_model};
    }
    
    for (size_t i = 0; i < models.size(); i++) {
        std::string marker = (models[i] == current_model) ? " [current]" : "";
        std::cout << "  " << (i + 1) << ". " << models[i] << marker << "\n";
    }
    std::cout << "  " << (models.size() + 1) << ". Enter custom model name\n";
    std::cout << "\n";
    
    std::cout << "Enter choice [1-" << (models.size() + 1) << "]: ";
    int choice;
    std::cin >> choice;
    std::cin.ignore();
    
    if (choice >= 1 && choice <= static_cast<int>(models.size())) {
        return models[choice - 1];
    } else if (choice == static_cast<int>(models.size()) + 1) {
        std::cout << "Enter model name: ";
        std::string custom;
        std::getline(std::cin >> std::ws, custom);
        return custom;
    }
    
    return current_model;
}

int UI::showApiKeyConfig(ConfigManager& config_mgr, ProviderType provider) {
    std::cout << "\n┌─────────────────────────────────────────────────────┐\n";
    std::cout << "│  API Key Configuration                                │\n";
    std::cout << "└─────────────────────────────────────────────────────┘\n\n";
    
    // Check if there's a saved API key
    const Config& config = config_mgr.getConfig();
    if (!config.api_key.empty()) {
        std::string masked = config_mgr.maskApiKey(config.api_key);
        std::cout << "Found previous API key: " << masked << "\n\n";
        std::cout << "  1. Use previous API key\n";
        std::cout << "  2. Enter new API key\n";
        std::cout << "\n";
        
        std::cout << "Enter choice [1-2]: ";
        int choice;
        std::cin >> choice;
        std::cin.ignore();
        
        if (choice == 1) {
            return 1;  // Use previous
        }
    } else {
        // Try to load from environment
        if (config_mgr.loadApiKeyFromEnv()) {
            std::string masked = config_mgr.maskApiKey(config_mgr.getConfig().api_key);
            std::cout << "Found API key in environment: " << masked << "\n\n";
            std::cout << "  1. Use environment API key\n";
            std::cout << "  2. Enter new API key\n";
            std::cout << "\n";
            
            std::cout << "Enter choice [1-2]: ";
            int choice;
            std::cin >> choice;
            std::cin.ignore();
            
            if (choice == 1) {
                return 1;  // Use environment
            }
        }
    }
    
    return 2;  // Enter new
}

} // namespace opencode
