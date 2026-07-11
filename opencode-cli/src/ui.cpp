#include "ui.hpp"
#include <iostream>
#include <sstream>

namespace opencode {

Ui::Ui() {}

void Ui::print_utf8_border(const std::string& text) {
    size_t width = text.length() + 4;
    
    // Top border
    std::cout << "\n╔";
    for (size_t i = 0; i < width; ++i) std::cout << "═";
    std::cout << "╗\n";
    
    // Content
    std::cout << "║ " << text << " ║\n";
    
    // Bottom border
    std::cout << "╚";
    for (size_t i = 0; i < width; ++i) std::cout << "═";
    std::cout << "╝\n";
}

void Ui::print_header() {
    std::cout << "\n";
    std::cout << " ┌─────────────────────────────────────────┐\n";
    std::cout << " │                                         │\n";
    std::cout << " │   ███╗   ██╗ ██████╗ ██╗   ██╗         │\n";
    std::cout << " │   ████╗  ██║██╔═══██╗██║   ██║         │\n";
    std::cout << " │   ██╔██╗ ██║██║   ██║██║   ██║         │\n";
    std::cout << " │   ██║╚██╗██║██║   ██║██║   ██║         │\n";
    std::cout << " │   ██║ ╚████║╚██████╔╝╚██████╔╝         │\n";
    std::cout << " │   ╚═╝  ╚═══╝ ╚═════╝  ╚═════╝          │\n";
    std::cout << " │                                         │\n";
    std::cout << " │   OpenCode CLI - AI-Powered Assistant  │\n";
    std::cout << " │                                         │\n";
    std::cout << " └─────────────────────────────────────────┘\n";
    std::cout << "\n";
}

void Ui::print_welcome() {
    print_utf8_border("Welcome to OpenCode CLI");
    std::cout << "\nYour AI-powered coding assistant.\n";
    std::cout << "Type 'help' for available commands, 'quit' to exit.\n\n";
}

std::string Ui::get_user_input(const std::string& prompt) {
    std::cout << "\n┌─[ " << prompt << " ]\n└─► ";
    std::string input;
    std::getline(std::cin, input);
    return input;
}

void Ui::print_message(const std::string& message, const std::string& role) {
    std::string prefix = (role == "user") ? "👤 You" : "🤖 Assistant";
    std::cout << "\n┌─ " << prefix << "\n";
    
    // Word wrap at ~80 chars
    std::istringstream iss(message);
    std::string word;
    std::string line;
    while (iss >> word) {
        if (line.length() + word.length() > 78) {
            std::cout << "│ " << line << "\n";
            line = word;
        } else {
            if (!line.empty()) line += " ";
            line += word;
        }
    }
    if (!line.empty()) {
        std::cout << "│ " << line << "\n";
    }
    std::cout << "└─\n";
}

void Ui::print_error(const std::string& error) {
    std::cout << "\n❌ Error: " << error << "\n\n";
}

void Ui::clear_screen() {
    std::cout << "\033[2J\033[H";
}

int Ui::show_menu(const std::string& title, const std::vector<std::string>& options) {
    std::cout << "\n";
    print_utf8_border(title);
    
    for (size_t i = 0; i < options.size(); ++i) {
        std::cout << "  [" << (i + 1) << "] " << options[i] << "\n";
    }
    
    std::cout << "\n► Enter choice (1-" << options.size() << "): ";
    int choice;
    std::cin >> choice;
    std::cin.ignore(10000, '\n');
    
    if (choice < 1 || choice > static_cast<int>(options.size())) {
        return -1;
    }
    return choice - 1;
}

std::string Ui::get_text_input(const std::string& prompt) {
    std::cout << "\n" << prompt << ": ";
    std::string input;
    std::getline(std::cin, input);
    return input;
}

} // namespace opencode
