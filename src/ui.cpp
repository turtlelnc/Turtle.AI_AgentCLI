#include "ui.hpp"
#include "text_formatter.hpp"
#include "diff_view.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <clocale>
#include <codecvt>
#include <locale>
#include <chrono>
#include <cstdlib>
#include <sstream>
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#define isatty _isatty
#ifndef STDOUT_FILENO
#define STDOUT_FILENO _fileno(stdout)
#endif
#else
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace opencode {

namespace {
struct CommandHelp {
    const char* command;
    const char* usage;
    const char* description;
};

const std::vector<CommandHelp>& commandCatalog() {
    static const std::vector<CommandHelp> commands = {
        {"/help", "/help", "Show all commands"},
        {"/stats", "/stats", "Show main and sub-agent usage"},
        {"/clear", "/clear", "Clear terminal output"},
        {"/theme", "/theme [name]", "Show or change color theme"},
        {"/animation", "/animation on|off", "Toggle thinking animation"},
        {"/mouse", "/mouse on|off", "Toggle mouse interaction and links"},
        {"/skills", "/skills", "List available skills"},
        {"/session", "/session", "Show current session ID"},
        {"/sessions", "/sessions", "List workspace sessions"},
        {"/resume", "/resume <id>", "Resume a saved session"},
        {"/goal", "/goal <text>|status|stop", "Manage persistent goal mode"},
        {"/subtask", "/subtask <text>|done <n>", "Manage goal subtasks"},
        {"/subtesk", "/subtesk <text>|done <n>", "Alias for /subtask"},
        {"/diff", "/diff", "Open the collapsed Git diff viewer"},
        {"/mcp", "/mcp [load <config.json>]", "Inspect or load MCP servers"},
        {"/memory", "/memory [global|project <text>]", "Read or write memory"},
        {"/mode", "/mode normal|goal|review", "Inspect or select mode"},
        {"/params", "/params [goal-max <1-256>]", "Inspect or adjust parameters"},
        {"/code-review", "/code-review [scope]", "Run a two-pass code review"},
        {"/changes", "/changes", "List reversible changes"},
        {"/undo", "/undo [change-id]", "Undo a safe change"},
        {"/exit", "/exit", "End the session"}
    };
    return commands;
}

constexpr std::size_t kTaskFooterLines = 10;
constexpr std::size_t kCommandRows = 6;
}

UI::UI()
    : terminal_interactive_(false),
      colors_enabled_(false),
      animations_enabled_(false),
      mouse_links_enabled_(false),
      thinking_(false),
      streaming_in_table_(false),
      streaming_has_lookahead_(false),
      streaming_direct_line_(false),
      theme_name_("ocean"),
      accent_color_("1;36"),
      success_color_("1;32"),
      warning_color_("1;33"),
      error_color_("1;31") {
    // 设置 UTF-8 locale + console
    std::setlocale(LC_ALL, ".UTF-8");
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &mode)) {
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, mode);
    }
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn != INVALID_HANDLE_VALUE && GetConsoleMode(hIn, &mode)) {
        mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
        SetConsoleMode(hIn, mode);
    }
#endif
    const char* no_color = std::getenv("NO_COLOR");
    terminal_interactive_ = isatty(STDOUT_FILENO);
    colors_enabled_ = terminal_interactive_ && no_color == nullptr;
    animations_enabled_ = terminal_interactive_;
    mouse_links_enabled_ = terminal_interactive_;
    const char* configured_theme = std::getenv("TURTLE_THEME");
    if (configured_theme) {
        setTheme(configured_theme);
    }
    const char* animations = std::getenv("TURTLE_ANIMATIONS");
    if (animations && std::string(animations) == "0") {
        animations_enabled_ = false;
    }
    const char* mouse_links = std::getenv("TURTLE_MOUSE");
    if (mouse_links && std::string(mouse_links) == "0") {
        mouse_links_enabled_ = false;
    }
}

UI::~UI() {
    stopThinking();
    if (task_panel_enabled_) {
        std::cout << "\033[r\033[?25h";
        std::cout.flush();
    }
}

std::string UI::color(const std::string& text, const std::string& code) const {
    if (!colors_enabled_) {
        return text;
    }
    return "\033[" + code + "m" + text + "\033[0m";
}

std::size_t UI::terminalColumns() const {
#if defined(_WIN32)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        CONSOLE_SCREEN_BUFFER_INFO csbi{};
        if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
            const std::size_t width = static_cast<std::size_t>(
                csbi.srWindow.Right - csbi.srWindow.Left + 1
            );
            if (width > 0) return width;
        }
    }
#else
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return size.ws_col;
    }
#endif
    const char* columns = std::getenv("COLUMNS");
    if (columns) {
        try {
            const auto parsed = std::stoul(columns);
            if (parsed > 0) return parsed;
        } catch (...) {
        }
    }
    return 80;
}

std::size_t UI::terminalRows() const {
#if defined(_WIN32)
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (output != INVALID_HANDLE_VALUE &&
        GetConsoleScreenBufferInfo(output, &info)) {
        return static_cast<std::size_t>(
            info.srWindow.Bottom - info.srWindow.Top + 1
        );
    }
#else
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row > 0) {
        return size.ws_row;
    }
#endif
    return 24;
}

void UI::enableTaskPanel() {
    if (!terminal_interactive_) return;
    task_panel_enabled_ = true;
    const std::size_t rows = terminalRows();
    const std::size_t output_bottom =
        rows > kTaskFooterLines ? rows - kTaskFooterLines : 1;
    std::cout << "\033[1;" << output_bottom << "r"
              << "\033[" << output_bottom << ";1H";
    drawTaskPanel();
}

void UI::setTaskPanel(
    const std::string& main_task,
    const std::vector<std::string>& subtasks,
    const std::string& usage
) {
    main_task_ = main_task.empty() ? "Main conversation" : main_task;
    panel_subtasks_ = subtasks;
    panel_usage_ = usage;
    if (selected_task_index_ > panel_subtasks_.size()) {
        selected_task_index_ = 0;
    }
    if (task_panel_enabled_) drawTaskPanel();
}

void UI::drawTaskPanel() {
    if (!task_panel_enabled_) return;
    const std::size_t rows = terminalRows();
    const std::size_t width = terminalColumns();
    const std::size_t panel_row =
        rows > kTaskFooterLines ? rows - kTaskFooterLines + 2 : 2;
    const std::string selected = selected_task_index_ == 0
        ? main_task_
        : panel_subtasks_[selected_task_index_ - 1];
    auto clipped = [&](std::string value) {
        if (value.size() > width) value.resize(width);
        return value;
    };
    std::cout << "\033[s"
              << "\033[" << panel_row << ";1H\033[2K"
              << color(
                  clipped(
                      "Task " + std::to_string(selected_task_index_ + 1) +
                      "/" + std::to_string(panel_subtasks_.size() + 1) +
                      ": " + selected
                  ),
                  accent_color_
              )
              << "\033[" << panel_row + 1 << ";1H\033[2K"
              << color(clipped(panel_usage_), "2")
              << "\033[" << panel_row + 2 << ";1H\033[2K"
              << color("↑/↓ switch task  •  type / for commands", "2")
              << "\033[u";
    std::cout.flush();
}

std::string UI::fileLink(const std::string& path) const {
    if (!mouse_links_enabled_ || path.empty() || path.front() == '<') {
        return path;
    }

    std::filesystem::path target(path);
    if (target.is_relative() && !workspace_root_.empty()) {
        target = workspace_root_ / target;
    }
    target = target.lexically_normal();

    std::string encoded;
    const std::string raw = target.string();
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char c : raw) {
        const bool safe =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '/' || c == '-' ||
            c == '_' || c == '.' || c == '~';
        if (safe) {
            encoded += static_cast<char>(c);
        } else {
            encoded += '%';
            encoded += hex[c >> 4];
            encoded += hex[c & 0x0F];
        }
    }
    return "\033]8;;file://" + encoded + "\033\\" + path + "\033]8;;\033\\";
}

void UI::renderMarkdown(const std::string& content) const {
    std::istringstream input(content);
    std::string line;
    bool in_code_block = false;
    while (std::getline(input, line)) {
        if (line.rfind("```", 0) == 0) {
            in_code_block = !in_code_block;
            if (in_code_block && line.size() > 3) {
                std::cout << color("[" + line.substr(3) + "]", "2") << '\n';
            }
            continue;
        }
        if (in_code_block) {
            std::cout << color("  " + line, accent_color_) << '\n';
            continue;
        }

        std::size_t heading_level = 0;
        while (heading_level < line.size() && line[heading_level] == '#') {
            ++heading_level;
        }
        if (heading_level > 0 && heading_level < line.size() &&
            line[heading_level] == ' ') {
            std::cout << color(line.substr(heading_level + 1), accent_color_)
                      << '\n';
            continue;
        }

        if (line == "---" || line == "***" || line == "___") {
            std::string separator;
            const std::size_t width = std::min<std::size_t>(terminalColumns(), 80);
            for (std::size_t index = 0; index < width; ++index) {
                separator += "─";
            }
            std::cout << color(separator, "2") << '\n';
            continue;
        }

        std::string rendered;
        for (std::size_t index = 0; index < line.size();) {
            if (line.compare(index, 2, "**") == 0) {
                const auto end = line.find("**", index + 2);
                if (end != std::string::npos) {
                    rendered += color(line.substr(index + 2, end - index - 2), "1");
                    index = end + 2;
                    continue;
                }
            }
            if (line[index] == '`') {
                const auto end = line.find('`', index + 1);
                if (end != std::string::npos) {
                    rendered += color(line.substr(index + 1, end - index - 1), accent_color_);
                    index = end + 1;
                    continue;
                }
            }
            rendered += line[index++];
        }
        std::cout << rendered << '\n';
    }
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
    if (terminalColumns() < 65) {
        std::cout << "\nTurtle.AI AgentCLI\n"
                  << "AI-powered coding assistant\n\n";
        return;
    }
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║                                                           ║\n";
    std::cout << "║   ███╗   ██╗ ██████╗ ██╗   ██╗ █████╗  ██████╗███████╗    ║\n";
    std::cout << "║   ████╗  ██║██╔═══██╗██║   ██║██╔══██╗██╔════╝██╔════╝    ║\n";
    std::cout << "║   ██╔██╗ ██║██║   ██║██║   ██║███████║██║     █████╗      ║\n";
    std::cout << "║   ██║╚██╗██║██║   ██║╚██╗ ██╔╝██╔══██║██║     ██╔══╝      ║\n";
    std::cout << "║   ██║ ╚████║╚██████╔╝ ╚████╔╝ ██║  ██║╚██████╗███████╗    ║\n";
    std::cout << "║   ╚═╝  ╚═══╝ ╚═════╝   ╚═══╝  ╚═╝  ╚═╝ ╚═════╝╚══════╝    ║\n";
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
    std::cout << "  - Multi-provider support (DeepSeek, OpenAI, Anthropic, LlamaCpp)\n";
    std::cout << "  - Built-in tool registry for file operations\n";
    std::cout << "  - Git integration with safety checks\n";
    std::cout << "  - Real-time token usage and cost tracking\n";
    std::cout << "\n";
}

int UI::showConfigWizard(ConfigManager& config_mgr, std::string& selected_model, bool& use_previous_api) {
    (void)config_mgr;
    (void)selected_model;
    (void)use_previous_api;
    std::cout << "\n[Configuration Wizard]\n\n";
    
    std::cout << "Select API Provider:\n";
    std::cout << "  1. DeepSeek (https://api.deepseek.com)\n";
    std::cout << "  2. OpenAI (https://api.openai.com)\n";
    std::cout << "  3. Anthropic (https://api.anthropic.com)\n";
    std::cout << "  4. LlamaCpp/Ollama (Local)\n";
    std::cout << "  5. OpenAI-compatible endpoint (Custom)\n";
    std::cout << "\n";
    
    while (true) {
        std::cout << "Enter choice [1-5]: ";
        std::string input;
        if (!std::getline(std::cin, input)) {
            return 0;
        }
        try {
            const int choice = std::stoi(input);
            if (choice >= 1 && choice <= 5) {
                return choice;
            }
        } catch (...) {
        }
        std::cout << "Please enter a number from 1 to 5.\n";
    }
}

std::string UI::getInput(const std::string& prompt) {
    if (task_panel_enabled_) return getTaskPanelInput(prompt);
    std::cout << "\n┌─[ " << prompt << " ]\n";
    std::cout << "└─► ";
    
    std::string input;
    const size_t MAX_INPUT_LENGTH = 4096;
    
    // 安全读取输入，防止过长导致崩溃
    if (!std::getline(std::cin, input)) {
        return "";
    }
    
    // 检查输入长度，超长时截断并提示
    if (input.length() > MAX_INPUT_LENGTH) {
        std::cout << "\nWarning: Input truncated from " << input.length()
                  << " to " << MAX_INPUT_LENGTH << " characters\n";
        input = input.substr(0, MAX_INPUT_LENGTH);
    }
    
    return input;
}

std::string UI::getTaskPanelInput(const std::string& prompt) {
    const std::size_t rows = terminalRows();
    const std::size_t output_bottom =
        rows > kTaskFooterLines ? rows - kTaskFooterLines : 1;
    const std::size_t input_row = output_bottom + 1;
    std::string input;
    std::size_t command_selection = 0;
    bool command_was_filled = false;
    auto matchingCommands = [&]() {
        std::vector<std::size_t> matches;
        if (input.empty() || input.front() != '/') return matches;
        std::string query = input.substr(1);
        const auto space = query.find(' ');
        if (space != std::string::npos) query.resize(space);
        std::transform(query.begin(), query.end(), query.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        const auto& catalog = commandCatalog();
        std::vector<std::pair<int, std::size_t>> ranked;
        for (std::size_t i = 0; i < catalog.size(); ++i) {
            std::string command_name = catalog[i].command + 1;
            std::transform(
                command_name.begin(), command_name.end(),
                command_name.begin(),
                [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                }
            );
            std::string haystack =
                command_name + " " +
                catalog[i].usage + " " + catalog[i].description;
            std::transform(
                haystack.begin(), haystack.end(), haystack.begin(),
                [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                }
            );
            if (query.empty() || command_name.rfind(query, 0) == 0) {
                ranked.push_back({0, i});
                continue;
            }
            if (haystack.find(query) != std::string::npos) {
                ranked.push_back({1, i});
                continue;
            }
            std::size_t cursor = 0;
            for (char ch : haystack) {
                if (cursor < query.size() && ch == query[cursor]) ++cursor;
            }
            if (cursor == query.size()) ranked.push_back({2, i});
        }
        std::stable_sort(ranked.begin(), ranked.end());
        for (const auto& entry : ranked) matches.push_back(entry.second);
        return matches;
    };
    auto drawCommands = [&]() {
        const auto matches = matchingCommands();
        if (command_selection >= matches.size()) command_selection = 0;
        const std::size_t first =
            command_selection >= kCommandRows
                ? command_selection - kCommandRows + 1 : 0;
        const auto& catalog = commandCatalog();
        for (std::size_t row = 0; row < kCommandRows; ++row) {
            std::cout << "\033[" << input_row + 4 + row
                      << ";1H\033[2K";
            const std::size_t match_index = first + row;
            if (match_index >= matches.size()) continue;
            const auto& command = catalog[matches[match_index]];
            std::string line =
                std::string(match_index == command_selection ? "› " : "  ") +
                command.usage + " — " + command.description;
            if (line.size() > terminalColumns()) {
                line.resize(terminalColumns());
            }
            std::cout << color(
                line,
                match_index == command_selection ? "1;36" : "2"
            );
        }
        if (!matches.empty()) {
            std::cout << "\033[" << input_row + 3 << ";1H\033[2K"
                      << color(
                          "Commands " +
                              std::to_string(command_selection + 1) + "/" +
                              std::to_string(matches.size()) +
                              "  ↑/↓ select • Enter fill",
                          "2"
                      );
        } else {
            std::cout << "\033[" << input_row + 3 << ";1H\033[2K"
                      << color(
                          "↑/↓ switch task  •  type / for commands",
                          "2"
                      );
        }
        std::cout.flush();
    };
    auto redraw = [&](bool redraw_panel = false) {
        if (redraw_panel) drawTaskPanel();
        std::cout << "\033[" << input_row << ";1H\033[2K"
                  << color(prompt + " › ", accent_color_) << input;
        drawCommands();
        std::cout.flush();
    };
    redraw(true);

#if !defined(_WIN32)
    termios original{};
    if (tcgetattr(STDIN_FILENO, &original) != 0) return "";
    termios raw = original;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    while (true) {
        char ch = '\0';
        if (read(STDIN_FILENO, &ch, 1) != 1) break;
        if (ch == '\r' || ch == '\n') {
            const auto matches = matchingCommands();
            if (!matches.empty() && !command_was_filled) {
                input = commandCatalog()[matches[command_selection]].command;
                const std::string usage =
                    commandCatalog()[matches[command_selection]].usage;
                if (usage.find('<') != std::string::npos ||
                    usage.find('[') != std::string::npos) {
                    input += ' ';
                }
                command_was_filled = true;
                redraw();
                continue;
            }
            break;
        }
        if (ch == 127 || ch == '\b') {
            if (!input.empty()) input.pop_back();
            command_was_filled = false;
            command_selection = 0;
        } else if (ch == '\033') {
            char sequence[2]{};
            bool selection_changed = false;
            if (read(STDIN_FILENO, &sequence[0], 1) == 1 &&
                read(STDIN_FILENO, &sequence[1], 1) == 1 &&
                sequence[0] == '[') {
                const auto matches = matchingCommands();
                if (!matches.empty()) {
                    if (sequence[1] == 'A' && command_selection > 0) {
                        --command_selection;
                    } else if (sequence[1] == 'B' &&
                               command_selection + 1 < matches.size()) {
                        ++command_selection;
                    }
                } else if (sequence[1] == 'A' &&
                           selected_task_index_ > 0) {
                    --selected_task_index_;
                    selection_changed = true;
                } else if (sequence[1] == 'B' &&
                           selected_task_index_ < panel_subtasks_.size()) {
                    ++selected_task_index_;
                    selection_changed = true;
                }
            }
            redraw(selection_changed);
            continue;
        } else if (static_cast<unsigned char>(ch) >= 32 &&
                   input.size() < 4096) {
            input += ch;
            command_was_filled = false;
            command_selection = 0;
        }
        redraw();
    }
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
#else
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    while (true) {
        INPUT_RECORD record{};
        DWORD count = 0;
        if (!ReadConsoleInputW(handle, &record, 1, &count) || count == 0) break;
        if (record.EventType != KEY_EVENT ||
            !record.Event.KeyEvent.bKeyDown) continue;
        const WORD key = record.Event.KeyEvent.wVirtualKeyCode;
        const wchar_t ch = record.Event.KeyEvent.uChar.UnicodeChar;
        if (key == VK_RETURN) {
            const auto matches = matchingCommands();
            if (!matches.empty() && !command_was_filled) {
                input = commandCatalog()[matches[command_selection]].command;
                const std::string usage =
                    commandCatalog()[matches[command_selection]].usage;
                if (usage.find('<') != std::string::npos ||
                    usage.find('[') != std::string::npos) {
                    input += ' ';
                }
                command_was_filled = true;
                redraw();
                continue;
            }
            break;
        }
        bool selection_changed = false;
        const auto matches = matchingCommands();
        if ((key == VK_UP || key == VK_DOWN) && !matches.empty()) {
            if (key == VK_UP && command_selection > 0) {
                --command_selection;
            } else if (key == VK_DOWN &&
                       command_selection + 1 < matches.size()) {
                ++command_selection;
            }
        } else if (key == VK_UP && selected_task_index_ > 0) {
            --selected_task_index_;
            selection_changed = true;
        } else if (key == VK_DOWN &&
                   selected_task_index_ < panel_subtasks_.size()) {
            ++selected_task_index_;
            selection_changed = true;
        } else if (key == VK_BACK && !input.empty()) {
            input.pop_back();
            command_was_filled = false;
            command_selection = 0;
        } else if (ch >= 32 && ch < 127 && input.size() < 4096) {
            input += static_cast<char>(ch);
            command_was_filled = false;
            command_selection = 0;
        }
        redraw(selection_changed);
    }
#endif
    std::cout << "\033[" << input_row << ";1H\033[2K"
              << "\033[" << output_bottom << ";1H";
    std::cout.flush();
    return input;
}

std::string UI::getSecretInput(const std::string& prompt) {
    std::cout << "\n┌─[ " << prompt << " ]\n";
    std::cout << "└─► ";
    std::cout.flush();

#if !defined(_WIN32)
    termios original{};
    const bool is_terminal = isatty(STDIN_FILENO) &&
                             tcgetattr(STDIN_FILENO, &original) == 0;
    if (is_terminal) {
        termios hidden = original;
        hidden.c_lflag &= static_cast<tcflag_t>(~ECHO);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden);
    }
#endif

    std::string input;
    std::getline(std::cin, input);

#if !defined(_WIN32)
    if (is_terminal) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
        std::cout << '\n';
    }
#endif
    return input;
}

void UI::showMessage(const std::string& role, const std::string& content) {
    std::cout << "\n┌─" << color("[ " + role + " ]", accent_color_) << "\n";
    std::cout << "└─► " << content << "\n";
}

void UI::showAIResponse(const std::string& content) {
    std::cout << "\n" << color("[AI Response]", accent_color_) << "\n\n";
    renderMarkdown(formatMarkdownTables(
        fixUtf8Encoding(content), terminalColumns()
    ));
    std::cout << '\n';
}

void UI::beginStreamingResponse() {
    streaming_content_.clear();
    streaming_lookahead_.clear();
    streaming_table_lines_.clear();
    streaming_in_table_ = false;
    streaming_has_lookahead_ = false;
    streaming_direct_line_ = false;
    std::cout << "\n" << color("[AI Response]", accent_color_) << "\n\n";
    std::cout.flush();
}

void UI::appendStreamingChunk(const std::string& content) {
    streaming_content_ += content;
    if (streaming_direct_line_) {
        const size_t newline = streaming_content_.find('\n');
        if (newline == std::string::npos) {
            std::cout << fixUtf8Encoding(streaming_content_);
            streaming_content_.clear();
            std::cout.flush();
            return;
        }
        std::cout << fixUtf8Encoding(streaming_content_.substr(0, newline)) << '\n';
        streaming_content_.erase(0, newline + 1);
        streaming_direct_line_ = false;
    }

    size_t newline = 0;
    while ((newline = streaming_content_.find('\n')) != std::string::npos) {
        std::string line = streaming_content_.substr(0, newline);
        streaming_content_.erase(0, newline + 1);
        processStreamingLine(line);
    }

    if (!streaming_content_.empty() &&
        !streaming_in_table_ &&
        !streaming_has_lookahead_) {
        const auto first = streaming_content_.find_first_not_of(" \t");
        if (first != std::string::npos &&
            streaming_content_[first] != '|' &&
            streaming_content_[first] != '#' &&
            streaming_content_[first] != '`') {
            std::cout << fixUtf8Encoding(streaming_content_);
            streaming_content_.clear();
            streaming_direct_line_ = true;
        }
    }
    std::cout.flush();
}

void UI::endStreamingResponse(double elapsed_seconds) {
    if (!streaming_direct_line_ && !streaming_content_.empty()) {
        processStreamingLine(streaming_content_);
        streaming_content_.clear();
    }
    streaming_direct_line_ = false;
    if (streaming_in_table_) {
        flushStreamingTable();
    } else if (streaming_has_lookahead_) {
        if (streaming_lookahead_.empty()) {
            std::cout << '\n';
        } else {
            renderMarkdown(fixUtf8Encoding(streaming_lookahead_));
        }
        streaming_lookahead_.clear();
        streaming_has_lookahead_ = false;
    }
    std::cout << "\n\n" << color(
        "Completed in " + std::to_string(elapsed_seconds).substr(0, 4) + "s",
        success_color_
    ) << "\n\n";
    std::cout.flush();
}

void UI::processStreamingLine(const std::string& line) {
    auto trim_line = [](const std::string& value) {
        const auto first = value.find_first_not_of(" \t");
        if (first == std::string::npos) return std::string();
        const auto last = value.find_last_not_of(" \t");
        return value.substr(first, last - first + 1);
    };
    auto is_separator = [&](const std::string& value) {
        const std::string trimmed = trim_line(value);
        return trimmed.find('|') != std::string::npos &&
               trimmed.find('-') != std::string::npos &&
               trimmed.find_first_not_of("|-: \t") == std::string::npos;
    };

    if (streaming_in_table_) {
        if (!trim_line(line).empty() && line.find('|') != std::string::npos) {
            streaming_table_lines_.push_back(line);
            return;
        }
        flushStreamingTable();
        streaming_lookahead_ = line;
        streaming_has_lookahead_ = true;
        return;
    }

    if (!streaming_has_lookahead_) {
        streaming_lookahead_ = line;
        streaming_has_lookahead_ = true;
        return;
    }

    if (streaming_lookahead_.find('|') != std::string::npos &&
        is_separator(line)) {
        streaming_in_table_ = true;
        streaming_table_lines_ = {streaming_lookahead_, line};
        streaming_lookahead_.clear();
        streaming_has_lookahead_ = false;
        return;
    }

    if (streaming_lookahead_.empty()) {
        std::cout << '\n';
    } else {
        renderMarkdown(fixUtf8Encoding(streaming_lookahead_));
    }
    streaming_lookahead_ = line;
    streaming_has_lookahead_ = true;
}

void UI::flushStreamingTable() {
    if (streaming_table_lines_.empty()) {
        streaming_in_table_ = false;
        return;
    }
    std::ostringstream table;
    for (size_t index = 0; index < streaming_table_lines_.size(); ++index) {
        if (index > 0) table << '\n';
        table << streaming_table_lines_[index];
    }
    renderMarkdown(formatMarkdownTables(
        fixUtf8Encoding(table.str()), terminalColumns()
    ));
    streaming_table_lines_.clear();
    streaming_in_table_ = false;
}

void UI::startThinking() {
    stopThinking();
    if (!animations_enabled_) {
        std::cout << "\nThinking...\n";
        std::cout.flush();
        return;
    }

    thinking_ = true;
    thinking_thread_ = std::thread([this]() {
        const std::vector<std::string> frames = {
            "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"
        };
        size_t frame = 0;
        while (thinking_) {
            std::cout << "\r\033[2K  "
                      << color(frames[frame % frames.size()], accent_color_)
                      << " Thinking...";
            std::cout.flush();
            frame++;
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
        }
    });
}

void UI::stopThinking() {
    thinking_ = false;
    if (thinking_thread_.joinable()) {
        thinking_thread_.join();
        std::cout << "\r\033[2K";
        std::cout.flush();
    }
}

void UI::showTokenStats(int64_t input_tokens, int64_t output_tokens, double cost_usd) {
    std::cout << "\n[Token Usage & Cost]\n"
              << "  Input tokens:  " << input_tokens << '\n'
              << "  Output tokens: " << output_tokens << '\n'
              << "  Total cost:    $" << std::fixed << std::setprecision(6)
              << cost_usd << " USD\n";
}

void UI::showError(const std::string& message) {
    std::cout << "\n" << color("[Error] ", error_color_) << message << "\n";
}

bool UI::confirmToolCall(
    const std::string& action,
    const std::string& detail,
    const std::string& preview
) {
    constexpr size_t kMaxDetailLength = 240;
    std::string visible_detail = detail;
    if (visible_detail.size() > kMaxDetailLength) {
        visible_detail.resize(kMaxDetailLength);
        visible_detail += "...";
    }
    std::replace(visible_detail.begin(), visible_detail.end(), '\n', ' ');

    std::cout << "\n┌─" << color("[ Approval required ]", warning_color_) << "\n";
    std::cout << "│  " << action << "\n";
    const bool file_action =
        action == "Write or overwrite a file" || action == "Edit a file";
    std::cout << "│  " << (file_action ? fileLink(visible_detail) : visible_detail) << "\n";
    if (!preview.empty()) {
        if (file_action) {
            showCollapsibleDiff(preview, false);
        } else {
            std::string compact = preview;
            std::replace(compact.begin(), compact.end(), '\n', ' ');
            if (compact.size() > 240) {
                compact.resize(240);
                compact += "...";
            }
            std::cout << "│  " << color(compact, "2") << '\n';
        }
    }
    while (true) {
        std::cout << "└─► Allow once? [y/N, v=view diff] ";
        std::string answer;
        if (!std::getline(std::cin, answer)) return false;
        if ((answer == "v" || answer == "V") &&
            !preview.empty() && file_action) {
            showCollapsibleDiff(preview, true);
            continue;
        }
        return answer == "y" || answer == "Y" ||
               answer == "yes" || answer == "YES";
    }
}

void UI::showToolCall(const std::string& name, const std::string& detail) {
    std::string visible_detail = detail;
    std::replace(visible_detail.begin(), visible_detail.end(), '\n', ' ');
    if (visible_detail.size() > 160) {
        visible_detail.resize(160);
        visible_detail += "...";
    }
    std::cout << "\n" << color("[Tool] ", accent_color_)
              << name;
    if (!visible_detail.empty()) {
        const bool file_tool =
            name == "read_file" || name == "write_file" ||
            name == "edit_file" || name == "list_directory";
        std::cout << "  " << color(
            file_tool ? fileLink(visible_detail) : visible_detail, "2"
        );
    }
    std::cout << '\n';
}

void UI::showToolResult(
    const std::string& name,
    bool success,
    const std::string& summary
) {
    std::string visible_summary = summary;
    std::replace(visible_summary.begin(), visible_summary.end(), '\n', ' ');
    if (visible_summary.size() > 200) {
        visible_summary.resize(200);
        visible_summary += "...";
    }
    std::cout << color(success ? "[Done] " : "[Failed] ",
                       success ? success_color_ : error_color_)
              << name;
    if (!visible_summary.empty()) {
        std::cout << "  " << color(visible_summary, "2");
    }
    std::cout << '\n';
}

void UI::showCollapsibleDiff(
    const std::string& diff,
    bool allow_interaction
) {
    DiffDocument document = DiffDocument::parse(diff);
    if (document.empty()) {
        showMessage("Diff", "No file changes.");
        return;
    }

    std::vector<std::size_t> button_offsets;
    std::size_t rendered_lines = 0;
    auto render = [&]() {
        button_offsets.clear();
        rendered_lines = 0;
        std::cout << "\n" << color("[Diff — collapsed by default]", accent_color_)
                  << '\n';
        rendered_lines += 2;
        const std::size_t width = terminalColumns();
        for (std::size_t i = 0; i < document.files().size(); ++i) {
            const auto& file = document.files()[i];
            std::string path = file.path;
            const std::size_t max_path = width > 30 ? width - 30 : 24;
            if (path.size() > max_path) {
                path = "..." + path.substr(path.size() - max_path + 3);
            }
            button_offsets.push_back(rendered_lines);
            std::cout << color(
                std::string(file.expanded ? "[▼ " : "[▶ ") +
                    std::to_string(i + 1) + "]",
                file.expanded ? "1;36" : "1;33"
            ) << ' ' << fileLink(path) << "  "
              << color("+" + std::to_string(file.additions), success_color_)
              << " "
              << color("-" + std::to_string(file.deletions), error_color_)
              << '\n';
            ++rendered_lines;
            if (file.expanded) {
                std::istringstream lines(file.content);
                std::string line;
                while (std::getline(lines, line)) {
                    if (line.size() + 2 > width) {
                        line.resize(width > 5 ? width - 5 : width);
                        line += "...";
                    }
                    const std::string code =
                        (!line.empty() && line.front() == '+') ? success_color_ :
                        (!line.empty() && line.front() == '-') ? error_color_ : "2";
                    std::cout << "  " << color(line, code) << '\n';
                    ++rendered_lines;
                }
            }
        }
    };

    render();
    if (!allow_interaction || !terminal_interactive_) return;

    std::cout << color(
        "Click [▶/▼] or press a file number to toggle; Enter/q closes.",
        "2"
    ) << '\n';
    ++rendered_lines;

#if !defined(_WIN32)
    termios original{};
    if (tcgetattr(STDIN_FILENO, &original) != 0) return;
    termios raw = original;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    if (mouse_links_enabled_) {
        std::cout << "\033[?1000h\033[?1006h";
        std::cout.flush();
    }

    auto cursorRow = [&]() -> int {
        std::cout << "\033[6n";
        std::cout.flush();
        std::string response;
        char value = '\0';
        while (response.size() < 32) {
            fd_set ready;
            FD_ZERO(&ready);
            FD_SET(STDIN_FILENO, &ready);
            timeval timeout{0, 200000};
            if (select(
                    STDIN_FILENO + 1, &ready, nullptr, nullptr, &timeout
                ) <= 0) {
                return -1;
            }
            if (read(STDIN_FILENO, &value, 1) != 1) return -1;
            response += value;
            if (value == 'R') break;
        }
        int row = -1;
        int column = -1;
        return std::sscanf(
            response.c_str(), "\033[%d;%dR", &row, &column
        ) == 2 ? row : -1;
    };
    int render_end_row = cursorRow();

    bool done = false;
    while (!done) {
        char ch = '\0';
        if (read(STDIN_FILENO, &ch, 1) != 1) break;
        if (ch == '\r' || ch == '\n' || ch == 'q' || ch == 'Q') {
            done = true;
        } else if (ch >= '1' && ch <= '9') {
            const std::size_t index = static_cast<std::size_t>(ch - '1');
            if (index < document.files().size()) {
                document.files()[index].expanded =
                    !document.files()[index].expanded;
                const int anchor =
                    render_end_row - static_cast<int>(rendered_lines);
                if (anchor > 0) {
                    std::cout << "\033[" << anchor << ";1H\033[J";
                }
                render();
                std::cout << color(
                    "Click [▶/▼] or press a file number to toggle; Enter/q closes.",
                    "2"
                ) << '\n';
                ++rendered_lines;
                render_end_row = cursorRow();
            }
        } else if (ch == '\033' && mouse_links_enabled_) {
            std::string event(1, ch);
            while (event.size() < 48 &&
                   event.back() != 'M' && event.back() != 'm') {
                if (read(STDIN_FILENO, &ch, 1) != 1) break;
                event += ch;
            }
            int button = -1;
            int column = -1;
            int row = -1;
            if (std::sscanf(
                    event.c_str(), "\033[<%d;%d;%dM",
                    &button, &column, &row
                ) == 3 && button == 0 && column <= 8) {
                for (std::size_t i = 0; i < button_offsets.size(); ++i) {
                    const int button_row =
                        render_end_row - static_cast<int>(rendered_lines) +
                        static_cast<int>(button_offsets[i]);
                    if (row != button_row) continue;
                    document.files()[i].expanded =
                        !document.files()[i].expanded;
                    const int anchor =
                        render_end_row - static_cast<int>(rendered_lines);
                    if (anchor > 0) {
                        std::cout << "\033[" << anchor << ";1H\033[J";
                    }
                    render();
                    std::cout << color(
                        "Click [▶/▼] or press a file number to toggle; Enter/q closes.",
                        "2"
                    ) << '\n';
                    ++rendered_lines;
                    render_end_row = cursorRow();
                    break;
                }
            }
        }
    }
    if (mouse_links_enabled_) {
        std::cout << "\033[?1000l\033[?1006l";
    }
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
    std::cout << '\n';
#else
    HANDLE input_handle = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE output_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD original_mode = 0;
    if (input_handle == INVALID_HANDLE_VALUE ||
        !GetConsoleMode(input_handle, &original_mode)) {
        return;
    }
    DWORD mouse_mode =
        (original_mode | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS) &
        ~ENABLE_QUICK_EDIT_MODE;
    SetConsoleMode(input_handle, mouse_mode);
    auto cursorRow = [&]() {
        CONSOLE_SCREEN_BUFFER_INFO info{};
        return GetConsoleScreenBufferInfo(output_handle, &info)
            ? static_cast<int>(info.dwCursorPosition.Y) + 1 : -1;
    };
    int render_end_row = cursorRow();
    bool done = false;
    while (!done) {
        INPUT_RECORD record{};
        DWORD read_count = 0;
        if (!ReadConsoleInputW(input_handle, &record, 1, &read_count) ||
            read_count == 0) {
            break;
        }
        std::size_t toggle = document.files().size();
        if (record.EventType == KEY_EVENT &&
            record.Event.KeyEvent.bKeyDown) {
            const wchar_t ch = record.Event.KeyEvent.uChar.UnicodeChar;
            if (ch == L'\r' || ch == L'q' || ch == L'Q') {
                done = true;
            } else if (ch >= L'1' && ch <= L'9') {
                toggle = static_cast<std::size_t>(ch - L'1');
            }
        } else if (record.EventType == MOUSE_EVENT &&
                   record.Event.MouseEvent.dwButtonState &
                       FROM_LEFT_1ST_BUTTON_PRESSED &&
                   record.Event.MouseEvent.dwMousePosition.X <= 7) {
            const int clicked_row =
                static_cast<int>(record.Event.MouseEvent.dwMousePosition.Y) + 1;
            for (std::size_t i = 0; i < button_offsets.size(); ++i) {
                const int button_row =
                    render_end_row - static_cast<int>(rendered_lines) +
                    static_cast<int>(button_offsets[i]);
                if (clicked_row == button_row) {
                    toggle = i;
                    break;
                }
            }
        }
        if (toggle < document.files().size()) {
            document.files()[toggle].expanded =
                !document.files()[toggle].expanded;
            const int anchor =
                render_end_row - static_cast<int>(rendered_lines);
            if (anchor > 0) {
                std::cout << "\033[" << anchor << ";1H\033[J";
            }
            render();
            std::cout << color(
                "Click [▶/▼] or press a file number to toggle; Enter/q closes.",
                "2"
            ) << '\n';
            ++rendered_lines;
            render_end_row = cursorRow();
        }
    }
    SetConsoleMode(input_handle, original_mode);
#endif
}

void UI::showHelp() {
    std::cout << "\nAvailable commands:\n";
    for (const auto& command : commandCatalog()) {
        std::cout << "  " << std::left << std::setw(34)
                  << command.usage << command.description << '\n';
    }
    std::cout << "\nType / to search commands in the input panel.\n";
}

void UI::showAppearanceSettings() {
    std::cout << "\n[Appearance]\n"
              << "  Theme:      " << theme_name_ << '\n'
              << "  Colors:     " << (colors_enabled_ ? "enabled" : "disabled") << '\n'
              << "  Animation:  " << (animations_enabled_ ? "enabled" : "disabled") << '\n'
              << "  Mouse links:" << (mouse_links_enabled_ ? " enabled" : " disabled") << '\n'
              << "  Themes:     ocean, violet, amber, green, mono\n"
              << "\nUsage: /theme <name>\n";
}

bool UI::setTheme(const std::string& theme) {
    if (theme == "ocean") {
        theme_name_ = theme;
        accent_color_ = "1;36";
        success_color_ = "1;32";
        warning_color_ = "1;33";
        error_color_ = "1;31";
    } else if (theme == "violet") {
        theme_name_ = theme;
        accent_color_ = "1;35";
        success_color_ = "1;36";
        warning_color_ = "1;33";
        error_color_ = "1;31";
    } else if (theme == "amber") {
        theme_name_ = theme;
        accent_color_ = "1;33";
        success_color_ = "1;32";
        warning_color_ = "1;35";
        error_color_ = "1;31";
    } else if (theme == "green") {
        theme_name_ = theme;
        accent_color_ = "1;32";
        success_color_ = "1;36";
        warning_color_ = "1;33";
        error_color_ = "1;35";
    } else if (theme == "mono") {
        theme_name_ = theme;
        colors_enabled_ = false;
        return true;
    } else {
        return false;
    }

    const char* no_color = std::getenv("NO_COLOR");
    colors_enabled_ = terminal_interactive_ && no_color == nullptr;
    return true;
}

bool UI::setAnimations(bool enabled) {
    if (enabled && !terminal_interactive_) {
        return false;
    }
    animations_enabled_ = enabled;
    if (!enabled) {
        stopThinking();
    }
    return true;
}

bool UI::setMouseLinks(bool enabled) {
    if (enabled && !terminal_interactive_) {
        return false;
    }
    mouse_links_enabled_ = enabled;
    return true;
}

void UI::showConfigurationSummary(const Config& config) {
    std::error_code error;
    workspace_root_ = std::filesystem::weakly_canonical(config.work_dir, error);
    if (error) {
        workspace_root_ = config.work_dir;
    }
    std::cout << "\n" << color("Configuration complete", success_color_) << "\n"
              << "  Provider:  " << ConfigManager::providerToString(config.provider) << '\n'
              << "  Model:     " << config.model << '\n'
              << "  Workspace: " << config.work_dir << '\n'
              << "  Git:       " << (config.use_git ? "enabled" : "disabled") << "\n\n";
}

void UI::clearScreen() {
    std::cout << "\033[2J\033[H";
    std::cout.flush();
}

std::string UI::getUtf8Border() {
    return "═";
}

void UI::showHistory(const std::vector<std::pair<std::string, std::string>>& history) {
    std::cout << "\n[Conversation History]\n";
    
    for (const auto& [role, content] : history) {
        std::cout << "\n[" << role << "]: " << content.substr(0, 100);
        if (content.length() > 100) std::cout << "...";
        std::cout << "\n";
    }
}


std::string UI::showWorkspaceSelection(ConfigManager& config_mgr) {
    auto recent = config_mgr.getRecentWorkspaces(15, 30);
    
    std::cout << "\n[Select Working Directory]\n\n";
    
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
    (void)work_dir;
    auto conversations = config_mgr.getConversations();
    
    std::cout << "\n[Conversation Options]\n\n";
    
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
            models = {"deepseek-v4-flash", "deepseek-v4-pro"};
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
    
    (void)provider;
    // This legacy UI entry point only sees process-memory credentials.
    // Environment variables and Keychain access are handled by SecretStore.
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
    }
    
    return 2;  // Enter new
}

} // namespace opencode
