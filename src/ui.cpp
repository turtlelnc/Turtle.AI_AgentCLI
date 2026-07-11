#include "ui.hpp"
#include <iostream>
#include <iomanip>

namespace opencode {

UI::UI() {}

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
    
    // 简单换行处理
    size_t pos = 0;
    size_t line_len = 70;
    while (pos < content.length()) {
        size_t end = content.find('\n', pos);
        if (end == std::string::npos || end - pos > line_len) {
            end = pos + line_len;
            if (end > content.length()) end = content.length();
        }
        std::cout << "  " << content.substr(pos, end - pos) << "\n";
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
