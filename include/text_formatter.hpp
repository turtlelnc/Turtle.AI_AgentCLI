#ifndef TEXT_FORMATTER_HPP
#define TEXT_FORMATTER_HPP

#include <cstddef>
#include <string>

namespace opencode {

std::size_t terminalDisplayWidth(const std::string& text);
std::string formatMarkdownTables(
    const std::string& text,
    std::size_t max_width = 0
);

} // namespace opencode

#endif // TEXT_FORMATTER_HPP
