#include "text_formatter.hpp"

#include <clocale>
#include <iostream>
#include <sstream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

} // namespace

int main() {
    std::setlocale(LC_ALL, "");
    bool passed = true;

    passed &= expect(
        opencode::terminalDisplayWidth("中文A") == 5,
        "CJK characters use terminal column width"
    );
    passed &= expect(
        opencode::terminalDisplayWidth("👩‍💻") == 2,
        "joined emoji uses one terminal glyph"
    );
    passed &= expect(
        opencode::terminalDisplayWidth("🇨🇳") == 2,
        "regional indicator pair uses one flag glyph"
    );

    const std::string input =
        "Before\n\n"
        "| 项目 | Value |\n"
        "| --- | --- |\n"
        "| 中文 | x |\n"
        "| A | longer |\n\n"
        "After";
    const std::string expected =
        "Before\n\n"
        "┌────┬──────┐\n"
        "│项目│Value │\n"
        "├────┼──────┤\n"
        "│中文│x     │\n"
        "├────┼──────┤\n"
        "│A   │longer│\n"
        "└────┴──────┘\n\n"
        "After";

    const std::string formatted = opencode::formatMarkdownTables(input);
    passed &= expect(formatted == expected, "Markdown table is normalized");

    std::istringstream lines(formatted);
    std::string line;
    std::size_t table_width = 0;
    while (std::getline(lines, line)) {
        if (line.empty() ||
            (line.rfind("│", 0) != 0 &&
             line.rfind("┌", 0) != 0 &&
             line.rfind("├", 0) != 0 &&
             line.rfind("└", 0) != 0)) {
            continue;
        }
        const std::size_t width = opencode::terminalDisplayWidth(line);
        if (table_width == 0) {
            table_width = width;
        }
        passed &= expect(width == table_width, "all rendered table lines align");
    }

    const std::string rich_table = opencode::formatMarkdownTables(
        "| **名称** | `值` |\n"
        "| --- | --- |\n"
        "| [中文](https://example.com) | 👩‍💻 |\n"
        "| flag | 🇨🇳 |"
    );
    std::istringstream rich_lines(rich_table);
    table_width = 0;
    while (std::getline(rich_lines, line)) {
        const std::size_t width = opencode::terminalDisplayWidth(line);
        if (table_width == 0) {
            table_width = width;
        }
        passed &= expect(
            width == table_width,
            "rich Markdown and emoji table lines align"
        );
    }

    const std::string long_table =
        "| 功能名称 | 详细说明 |\n"
        "| --- | --- |\n"
        "| workspace sandbox | 这是一段会超过窗口宽度的中文说明文字 |\n"
        "| streaming | long English content that must wrap |";
    for (const std::size_t viewport : {std::size_t{20}, std::size_t{32}}) {
        const std::string responsive =
            opencode::formatMarkdownTables(long_table, viewport);
        std::istringstream responsive_lines(responsive);
        table_width = 0;
        while (std::getline(responsive_lines, line)) {
            const std::size_t width = opencode::terminalDisplayWidth(line);
            passed &= expect(
                width <= viewport,
                "responsive table does not exceed viewport"
            );
            if (table_width == 0) {
                table_width = width;
            }
            passed &= expect(
                width == table_width,
                "wrapped physical table lines stay aligned"
            );
        }
    }

    return passed ? 0 : 1;
}
