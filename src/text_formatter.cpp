#include "text_formatter.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <numeric>
#include <sstream>
#include <vector>

namespace {

/// Decode the next UTF-8 codepoint.  Returns (codepoint | bytes_consumed)
/// where bytes_consumed occupies the low 8 bits.  Returns 0xFFFFFFFF on
/// error; width callers treat invalid sequences as width-1 replacement
/// characters.
uint32_t utf8Decode(const char* data, std::size_t remaining) {
    if (remaining == 0) return static_cast<uint32_t>(-1);
    const unsigned char lead = static_cast<unsigned char>(data[0]);

    if (lead < 0x80) return (static_cast<uint32_t>(lead) << 8) | 1;

    std::size_t length = 1;
    uint32_t codepoint = 0;
    if ((lead & 0xE0) == 0xC0) {
        codepoint = lead & 0x1F;
        length = 2;
    } else if ((lead & 0xF0) == 0xE0) {
        codepoint = lead & 0x0F;
        length = 3;
    } else if ((lead & 0xF8) == 0xF0) {
        codepoint = lead & 0x07;
        length = 4;
    } else {
        return (static_cast<uint32_t>('?') << 8) | 1;
    }

    if (length > remaining) return (static_cast<uint32_t>('?') << 8) | 1;

    for (std::size_t i = 1; i < length; ++i) {
        const unsigned char cont = static_cast<unsigned char>(data[i]);
        if ((cont & 0xC0) != 0x80) return (static_cast<uint32_t>('?') << 8) | 1;
        codepoint = (codepoint << 6) | (cont & 0x3F);
    }

    // Reject overlong sequences and surrogates.
    if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
        return (static_cast<uint32_t>('?') << 8) | 1;

    return (codepoint << 8) | static_cast<uint32_t>(length);
}

/// Terminal column width of a Unicode codepoint.  Follows the same
/// heuristics as POSIX wcwidth(3) / Unicode TR11 East Asian Width.
int codepointWidth(uint32_t cp) {
    if (cp == 0) return 0;

    // Control characters and zero-width marks.
    if (cp < 0x20) return 0;
    if (cp >= 0x200B && cp <= 0x200F) return 0;   // ZW space, LTR/RTL marks
    if (cp >= 0x2028 && cp <= 0x2029) return 0;    // line/paragraph separator
    if (cp >= 0x202A && cp <= 0x202E) return 0;    // bidi controls
    if (cp >= 0x2060 && cp <= 0x2064) return 0;    // word joiner, invisible
    if (cp == 0xFEFF) return 0;                     // BOM / ZWNBSP
    if (cp >= 0xFFF9 && cp <= 0xFFFB) return 0;    // interlinear annotations
    if (cp >= 0xE0100 && cp <= 0xE01EF) return 0;  // variation selectors

    // Combining marks (Mn, Mc, Me).
    if ((cp >= 0x0300 && cp <= 0x036F) ||
        (cp >= 0x0483 && cp <= 0x0489) ||
        (cp >= 0x0591 && cp <= 0x05BD) ||
        (cp >= 0x0610 && cp <= 0x061A) ||
        (cp >= 0x064B && cp <= 0x065F) ||
        (cp >= 0x0670 && cp <= 0x0670) ||
        (cp >= 0x06D6 && cp <= 0x06DC) ||
        (cp >= 0x06DF && cp <= 0x06E4) ||
        (cp >= 0x06E7 && cp <= 0x06E8) ||
        (cp >= 0x06EA && cp <= 0x06ED) ||
        (cp >= 0x0711 && cp <= 0x0711) ||
        (cp >= 0x0730 && cp <= 0x074A) ||
        (cp >= 0x07A6 && cp <= 0x07B0) ||
        (cp >= 0x0900 && cp <= 0x0902) ||
        (cp >= 0x093A && cp <= 0x093C) ||
        (cp >= 0x0941 && cp <= 0x0948) ||
        (cp >= 0x0951 && cp <= 0x0957) ||
        (cp >= 0x0962 && cp <= 0x0963) ||
        (cp >= 0x0981 && cp <= 0x0981) ||
        (cp >= 0x09BC && cp <= 0x09BC) ||
        (cp >= 0x09C1 && cp <= 0x09C4) ||
        (cp >= 0x09CD && cp <= 0x09CD) ||
        (cp >= 0x09E2 && cp <= 0x09E3) ||
        (cp >= 0x0A01 && cp <= 0x0A02) ||
        (cp >= 0x0A3C && cp <= 0x0A3C) ||
        (cp >= 0x0A41 && cp <= 0x0A42) ||
        (cp >= 0x0A47 && cp <= 0x0A48) ||
        (cp >= 0x0A4B && cp <= 0x0A4D) ||
        (cp >= 0x0A70 && cp <= 0x0A71) ||
        (cp >= 0x0A81 && cp <= 0x0A82) ||
        (cp >= 0x0ABC && cp <= 0x0ABC) ||
        (cp >= 0x0AC1 && cp <= 0x0AC5) ||
        (cp >= 0x0AC7 && cp <= 0x0AC8) ||
        (cp >= 0x0ACD && cp <= 0x0ACD) ||
        (cp >= 0x0AE2 && cp <= 0x0AE3) ||
        (cp >= 0x0B01 && cp <= 0x0B01) ||
        (cp >= 0x0B3C && cp <= 0x0B3C) ||
        (cp >= 0x0B3F && cp <= 0x0B3F) ||
        (cp >= 0x0B41 && cp <= 0x0B44) ||
        (cp >= 0x0B4D && cp <= 0x0B4D) ||
        (cp >= 0x0B56 && cp <= 0x0B56) ||
        (cp >= 0x0B62 && cp <= 0x0B63) ||
        (cp >= 0x0B82 && cp <= 0x0B82) ||
        (cp >= 0x0BC0 && cp <= 0x0BC0) ||
        (cp >= 0x0BCD && cp <= 0x0BCD) ||
        (cp >= 0x0C3E && cp <= 0x0C40) ||
        (cp >= 0x0C46 && cp <= 0x0C48) ||
        (cp >= 0x0C4A && cp <= 0x0C4D) ||
        (cp >= 0x0C55 && cp <= 0x0C56) ||
        (cp >= 0x0C62 && cp <= 0x0C63) ||
        (cp >= 0x0CBC && cp <= 0x0CBC) ||
        (cp >= 0x0CBF && cp <= 0x0CBF) ||
        (cp >= 0x0CC6 && cp <= 0x0CC6) ||
        (cp >= 0x0CCC && cp <= 0x0CCD) ||
        (cp >= 0x0CE2 && cp <= 0x0CE3) ||
        (cp >= 0x0D41 && cp <= 0x0D44) ||
        (cp >= 0x0D4D && cp <= 0x0D4D) ||
        (cp >= 0x0D62 && cp <= 0x0D63) ||
        (cp >= 0x0DCA && cp <= 0x0DCA) ||
        (cp >= 0x0DD2 && cp <= 0x0DD4) ||
        (cp >= 0x0DD6 && cp <= 0x0DD6) ||
        (cp >= 0x0E31 && cp <= 0x0E31) ||
        (cp >= 0x0E34 && cp <= 0x0E3A) ||
        (cp >= 0x0E47 && cp <= 0x0E4E) ||
        (cp >= 0x0EB1 && cp <= 0x0EB1) ||
        (cp >= 0x0EB4 && cp <= 0x0EB9) ||
        (cp >= 0x0EBB && cp <= 0x0EBC) ||
        (cp >= 0x0EC8 && cp <= 0x0ECD) ||
        (cp >= 0x0F18 && cp <= 0x0F19) ||
        (cp >= 0x0F35 && cp <= 0x0F35) ||
        (cp >= 0x0F37 && cp <= 0x0F37) ||
        (cp >= 0x0F39 && cp <= 0x0F39) ||
        (cp >= 0x0F71 && cp <= 0x0F7E) ||
        (cp >= 0x0F80 && cp <= 0x0F84) ||
        (cp >= 0x0F86 && cp <= 0x0F87) ||
        (cp >= 0x0F90 && cp <= 0x0F97) ||
        (cp >= 0x0F99 && cp <= 0x0FBC) ||
        (cp >= 0x0FC6 && cp <= 0x0FC6) ||
        (cp >= 0x102D && cp <= 0x1030) ||
        (cp >= 0x1032 && cp <= 0x1037) ||
        (cp >= 0x1039 && cp <= 0x103A) ||
        (cp >= 0x103D && cp <= 0x103E) ||
        (cp >= 0x1058 && cp <= 0x1059) ||
        (cp >= 0x105E && cp <= 0x1060) ||
        (cp >= 0x1071 && cp <= 0x1074) ||
        (cp >= 0x1082 && cp <= 0x1082) ||
        (cp >= 0x1085 && cp <= 0x1086) ||
        (cp >= 0x108D && cp <= 0x108D) ||
        (cp >= 0x109D && cp <= 0x109D) ||
        (cp >= 0x135D && cp <= 0x135F) ||
        (cp >= 0x1712 && cp <= 0x1714) ||
        (cp >= 0x1732 && cp <= 0x1734) ||
        (cp >= 0x1752 && cp <= 0x1753) ||
        (cp >= 0x1772 && cp <= 0x1773) ||
        (cp >= 0x17B4 && cp <= 0x17B5) ||
        (cp >= 0x17B7 && cp <= 0x17BD) ||
        (cp >= 0x17C6 && cp <= 0x17C6) ||
        (cp >= 0x17C9 && cp <= 0x17D3) ||
        (cp >= 0x17DD && cp <= 0x17DD) ||
        (cp >= 0x180B && cp <= 0x180D) ||
        (cp >= 0x1885 && cp <= 0x1886) ||
        (cp >= 0x18A9 && cp <= 0x18A9) ||
        (cp >= 0x1920 && cp <= 0x1922) ||
        (cp >= 0x1927 && cp <= 0x1928) ||
        (cp >= 0x1932 && cp <= 0x1932) ||
        (cp >= 0x1939 && cp <= 0x193B) ||
        (cp >= 0x1A17 && cp <= 0x1A18) ||
        (cp >= 0x1A56 && cp <= 0x1A56) ||
        (cp >= 0x1A58 && cp <= 0x1A5E) ||
        (cp >= 0x1A60 && cp <= 0x1A60) ||
        (cp >= 0x1A62 && cp <= 0x1A62) ||
        (cp >= 0x1A65 && cp <= 0x1A6C) ||
        (cp >= 0x1A73 && cp <= 0x1A7C) ||
        (cp >= 0x1A7F && cp <= 0x1A7F) ||
        (cp >= 0x1B00 && cp <= 0x1B03) ||
        (cp >= 0x1B34 && cp <= 0x1B34) ||
        (cp >= 0x1B36 && cp <= 0x1B3A) ||
        (cp >= 0x1B3C && cp <= 0x1B3C) ||
        (cp >= 0x1B42 && cp <= 0x1B42) ||
        (cp >= 0x1B6B && cp <= 0x1B73) ||
        (cp >= 0x1B80 && cp <= 0x1B81) ||
        (cp >= 0x1BA2 && cp <= 0x1BA5) ||
        (cp >= 0x1BA8 && cp <= 0x1BA9) ||
        (cp >= 0x1BAB && cp <= 0x1BAD) ||
        (cp >= 0x1BE6 && cp <= 0x1BE6) ||
        (cp >= 0x1BE8 && cp <= 0x1BE9) ||
        (cp >= 0x1BED && cp <= 0x1BED) ||
        (cp >= 0x1BEF && cp <= 0x1BF1) ||
        (cp >= 0x1C2C && cp <= 0x1C33) ||
        (cp >= 0x1C36 && cp <= 0x1C37) ||
        (cp >= 0x1CD0 && cp <= 0x1CD2) ||
        (cp >= 0x1CD4 && cp <= 0x1CE0) ||
        (cp >= 0x1CE2 && cp <= 0x1CE8) ||
        (cp >= 0x1CED && cp <= 0x1CED) ||
        (cp >= 0x1CF4 && cp <= 0x1CF4) ||
        (cp >= 0x1DC0 && cp <= 0x1DFF) ||
        (cp >= 0x20D0 && cp <= 0x20F0) ||
        (cp >= 0x2CEF && cp <= 0x2CF1) ||
        (cp >= 0x2D7F && cp <= 0x2D7F) ||
        (cp >= 0x2DE0 && cp <= 0x2DFF) ||
        (cp >= 0xA66F && cp <= 0xA672) ||
        (cp >= 0xA674 && cp <= 0xA67D) ||
        (cp >= 0xA69E && cp <= 0xA69F) ||
        (cp >= 0xA6F0 && cp <= 0xA6F1) ||
        (cp >= 0xA802 && cp <= 0xA802) ||
        (cp >= 0xA806 && cp <= 0xA806) ||
        (cp >= 0xA80B && cp <= 0xA80B) ||
        (cp >= 0xA825 && cp <= 0xA826) ||
        (cp >= 0xA8C4 && cp <= 0xA8C5) ||
        (cp >= 0xA8E0 && cp <= 0xA8F1) ||
        (cp >= 0xA926 && cp <= 0xA92D) ||
        (cp >= 0xA947 && cp <= 0xA951) ||
        (cp >= 0xA980 && cp <= 0xA982) ||
        (cp >= 0xA9B3 && cp <= 0xA9B3) ||
        (cp >= 0xA9B6 && cp <= 0xA9B9) ||
        (cp >= 0xA9BC && cp <= 0xA9BC) ||
        (cp >= 0xAA29 && cp <= 0xAA2E) ||
        (cp >= 0xAA31 && cp <= 0xAA32) ||
        (cp >= 0xAA35 && cp <= 0xAA36) ||
        (cp >= 0xAA43 && cp <= 0xAA43) ||
        (cp >= 0xAA4C && cp <= 0xAA4C) ||
        (cp >= 0xAAB0 && cp <= 0xAAB0) ||
        (cp >= 0xAAB2 && cp <= 0xAAB4) ||
        (cp >= 0xAAB7 && cp <= 0xAAB8) ||
        (cp >= 0xAABE && cp <= 0xAABF) ||
        (cp >= 0xAAC1 && cp <= 0xAAC1) ||
        (cp >= 0xAAEC && cp <= 0xAAED) ||
        (cp >= 0xAAF6 && cp <= 0xAAF6) ||
        (cp >= 0xABE5 && cp <= 0xABE5) ||
        (cp >= 0xABE8 && cp <= 0xABE8) ||
        (cp >= 0xABED && cp <= 0xABED) ||
        (cp >= 0xFB1E && cp <= 0xFB1E) ||
        (cp >= 0xFE00 && cp <= 0xFE0F) ||
        (cp >= 0xFE20 && cp <= 0xFE2F))
        return 0;

    // East Asian Wide / Fullwidth (Unicode TR11).
    if ((cp >= 0x1100 && cp <= 0x115F) ||   // Hangul Jamo
        (cp >= 0x2329 && cp <= 0x232A) ||   // angle brackets
        (cp >= 0x2E80 && cp <= 0x303E) ||   // CJK Radicals … CJK Symbols
        (cp >= 0x3041 && cp <= 0x33BF) ||   // Hiragana … CJK Compat
        (cp >= 0x3400 && cp <= 0x4DBF) ||   // CJK Ext-A
        (cp >= 0x4E00 && cp <= 0xA4CF) ||   // CJK Unified … Yi
        (cp >= 0xA960 && cp <= 0xA97C) ||   // Hangul Jamo Ext-A
        (cp >= 0xAC00 && cp <= 0xD7A3) ||   // Hangul Syllables
        (cp >= 0xF900 && cp <= 0xFAFF) ||   // CJK Compat Ideographs
        (cp >= 0xFE10 && cp <= 0xFE19) ||   // Vertical forms
        (cp >= 0xFE30 && cp <= 0xFE6F) ||   // CJK Compat Forms
        (cp >= 0xFF01 && cp <= 0xFF60) ||   // Fullwidth Forms
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||   // Fullwidth Signs
        (cp >= 0x1B000 && cp <= 0x1B2FF) || // Kana Supplement … Small Kana Ext
        (cp >= 0x1F200 && cp <= 0x1F2FF) || // Enclosed Ideographic Suppl
        (cp >= 0x20000 && cp <= 0x2FFFD) || // CJK Ext-B … last SIP
        (cp >= 0x30000 && cp <= 0x3FFFD))   // CJK Ext-G … last TIP
        return 2;

    // Emoji / symbols that occupy 2 terminal columns.
    if (cp >= 0x1F000 && cp <= 0x1F9FF) return 2;  // Emoji & Symbols & Pictographs
    if (cp >= 0x1FA00 && cp <= 0x1FAFF) return 2;  // Chess Symbols …
    if (cp >= 0x1F300 && cp <= 0x1F6FF) return 2;  // (covered above but belt-and-suspenders)

    // Everything else is single-width.
    return 1;
}

} // namespace

namespace opencode {

namespace {

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

std::string visibleMarkdownCell(std::string value) {
    for (const std::string marker : {"**", "__", "~~"}) {
        size_t position = 0;
        while ((position = value.find(marker, position)) != std::string::npos) {
            value.erase(position, marker.size());
        }
    }
    value.erase(
        std::remove(value.begin(), value.end(), '`'),
        value.end()
    );

    // Markdown links display only their label in a terminal table.
    size_t start = 0;
    while ((start = value.find('[', start)) != std::string::npos) {
        const size_t label_end = value.find("](", start + 1);
        const size_t target_end =
            label_end == std::string::npos ? std::string::npos :
            value.find(')', label_end + 2);
        if (label_end == std::string::npos || target_end == std::string::npos) {
            break;
        }
        const std::string label =
            value.substr(start + 1, label_end - start - 1);
        value.replace(start, target_end - start + 1, label);
        start += label.size();
    }

    size_t tab = 0;
    while ((tab = value.find('\t', tab)) != std::string::npos) {
        value.replace(tab, 1, "    ");
        tab += 4;
    }
    return value;
}

std::vector<std::string> splitTableRow(const std::string& line) {
    std::string body = trim(line);
    if (!body.empty() && body.front() == '|') {
        body.erase(body.begin());
    }
    if (!body.empty() && body.back() == '|') {
        body.pop_back();
    }

    std::vector<std::string> cells;
    std::string cell;
    bool escaped = false;
    for (char c : body) {
        if (c == '|' && !escaped) {
            cells.push_back(visibleMarkdownCell(trim(cell)));
            cell.clear();
        } else {
            cell += c;
        }
        escaped = c == '\\' && !escaped;
        if (c != '\\') {
            escaped = false;
        }
    }
    cells.push_back(visibleMarkdownCell(trim(cell)));
    return cells;
}

bool isSeparatorCell(const std::string& cell) {
    std::string value = trim(cell);
    if (!value.empty() && value.front() == ':') {
        value.erase(value.begin());
    }
    if (!value.empty() && value.back() == ':') {
        value.pop_back();
    }
    return value.size() >= 3 &&
           std::all_of(value.begin(), value.end(), [](char c) { return c == '-'; });
}

bool isSeparatorRow(const std::string& line) {
    const auto cells = splitTableRow(line);
    return !cells.empty() &&
           std::all_of(cells.begin(), cells.end(), isSeparatorCell);
}

std::string padCell(const std::string& cell, std::size_t width) {
    const std::size_t current = terminalDisplayWidth(cell);
    return cell + std::string(width > current ? width - current : 0, ' ');
}

std::vector<std::string> wrapCell(const std::string& cell, std::size_t width) {
    if (cell.empty()) {
        return {""};
    }

    std::vector<std::string> lines;
    std::string current;
    for (std::size_t offset = 0; offset < cell.size();) {
        const unsigned char first = static_cast<unsigned char>(cell[offset]);
        std::size_t length = 1;
        if ((first & 0xE0) == 0xC0) length = 2;
        else if ((first & 0xF0) == 0xE0) length = 3;
        else if ((first & 0xF8) == 0xF0) length = 4;
        length = std::min(length, cell.size() - offset);

        const std::string character = cell.substr(offset, length);
        const std::string candidate = current + character;
        if (!current.empty() && terminalDisplayWidth(candidate) > width) {
            lines.push_back(current);
            current = character;
        } else {
            current = candidate;
        }
        offset += length;
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

std::string tableBorder(
    const std::vector<std::size_t>& widths,
    const std::string& left,
    const std::string& middle,
    const std::string& right
) {
    std::ostringstream output;
    output << left;
    for (std::size_t column = 0; column < widths.size(); ++column) {
        for (std::size_t index = 0; index < widths[column]; ++index) {
            output << "─";
        }
        if (column + 1 < widths.size()) {
            output << middle;
        }
    }
    output << right;
    return output.str();
}

std::string renderTable(
    const std::vector<std::string>& lines,
    std::size_t max_width
) {
    std::vector<std::vector<std::string>> rows;
    std::size_t column_count = 0;
    for (const auto& line : lines) {
        rows.push_back(splitTableRow(line));
        column_count = std::max(column_count, rows.back().size());
    }

    std::vector<std::size_t> widths(column_count, 3);
    for (std::size_t row = 0; row < rows.size(); ++row) {
        if (row == 1 && isSeparatorRow(lines[row])) {
            continue;
        }
        for (std::size_t column = 0; column < rows[row].size(); ++column) {
            widths[column] = std::max(
                widths[column], terminalDisplayWidth(rows[row][column])
            );
        }
    }

    const std::size_t border_width = column_count + 1;
    const std::size_t natural_width =
        border_width +
        std::accumulate(widths.begin(), widths.end(), std::size_t{0});
    if (max_width > border_width + column_count * 3 &&
        natural_width > max_width) {
        const std::size_t available = max_width - border_width;
        std::vector<std::size_t> constrained(column_count, 3);
        std::size_t remaining = available - column_count * 3;
        while (remaining > 0) {
            bool assigned = false;
            for (std::size_t column = 0;
                 column < column_count && remaining > 0;
                 ++column) {
                if (constrained[column] < widths[column]) {
                    ++constrained[column];
                    --remaining;
                    assigned = true;
                }
            }
            if (!assigned) break;
        }
        widths = std::move(constrained);
    }

    // The Markdown separator row controls alignment but is not rendered as
    // content. Terminal tables use connected box-drawing borders without
    // extra left/right cell margins.
    std::vector<std::vector<std::string>> content_rows;
    for (std::size_t row = 0; row < rows.size(); ++row) {
        if (row != 1 || !isSeparatorRow(lines[row])) {
            content_rows.push_back(rows[row]);
        }
    }

    std::ostringstream output;
    output << tableBorder(widths, "┌", "┬", "┐") << '\n';
    for (std::size_t row = 0; row < content_rows.size(); ++row) {
        std::vector<std::vector<std::string>> wrapped(column_count);
        std::size_t row_height = 1;
        for (std::size_t column = 0; column < column_count; ++column) {
            const std::string cell =
                column < content_rows[row].size() ? content_rows[row][column] : "";
            wrapped[column] = wrapCell(cell, widths[column]);
            row_height = std::max(row_height, wrapped[column].size());
        }
        for (std::size_t physical_row = 0;
             physical_row < row_height;
             ++physical_row) {
            output << "│";
            for (std::size_t column = 0; column < column_count; ++column) {
                const std::string fragment =
                    physical_row < wrapped[column].size()
                    ? wrapped[column][physical_row]
                    : "";
                output << padCell(fragment, widths[column]) << "│";
            }
            output << '\n';
        }
        if (row + 1 < content_rows.size()) {
            output << tableBorder(widths, "├", "┼", "┤") << '\n';
        } else {
            output << tableBorder(widths, "└", "┴", "┘");
        }
    }
    return output.str();
}

} // namespace

std::size_t terminalDisplayWidth(const std::string& text) {
    std::size_t width = 0;
    const char* current = text.data();
    std::size_t remaining = text.size();

    while (remaining > 0) {
        // Skip ANSI CSI escape sequences (ESC [ ... final).
        if (remaining >= 2 && current[0] == '\033' && current[1] == '[') {
            current += 2;
            remaining -= 2;
            while (remaining > 0) {
                const unsigned char c = static_cast<unsigned char>(*current);
                ++current;
                --remaining;
                if (c >= 0x40 && c <= 0x7E) break;
            }
            continue;
        }
        // Skip ANSI OSC escape sequences (ESC ] ... BEL or ESC \).
        if (remaining >= 2 && current[0] == '\033' && current[1] == ']') {
            current += 2;
            remaining -= 2;
            while (remaining > 0) {
                if (*current == '\a') {
                    ++current;
                    --remaining;
                    break;
                }
                if (remaining >= 2 && current[0] == '\033' && current[1] == '\\') {
                    current += 2;
                    remaining -= 2;
                    break;
                }
                ++current;
                --remaining;
            }
            continue;
        }

        // Decode one UTF-8 codepoint.
        const uint32_t decoded = utf8Decode(current, remaining);
        const uint32_t cp = decoded >> 8;
        const std::size_t consumed = decoded & 0xFF;

        if (consumed == 0 || consumed > remaining) {
            ++current;
            --remaining;
            ++width;
            continue;
        }

        current += consumed;
        remaining -= consumed;

        int cw = codepointWidth(cp);
        bool joined_emoji = false;
        bool regional_pair = false;

        // Regional indicator pair → one flag glyph (width 2).
        if (cp >= 0x1F1E6 && cp <= 0x1F1FF && remaining > 0) {
            const uint32_t next_decoded = utf8Decode(current, remaining);
            const uint32_t next_cp = next_decoded >> 8;
            const std::size_t next_consumed = next_decoded & 0xFF;
            if (next_consumed > 0 && next_consumed <= remaining &&
                next_cp >= 0x1F1E6 && next_cp <= 0x1F1FF) {
                regional_pair = true;
                current += next_consumed;
                remaining -= next_consumed;
            }
        }

        // Zero-width joiner + emoji cluster → count as 2 columns.
        while (remaining > 0) {
            const uint32_t next_decoded = utf8Decode(current, remaining);
            const uint32_t next_cp = next_decoded >> 8;
            const std::size_t next_consumed = next_decoded & 0xFF;

            if (next_consumed == 0 || next_consumed > remaining) break;

            // Skin-tone modifier or variation selector → consume, don't add width.
            const bool modifier =
                (next_cp >= 0xFE00 && next_cp <= 0xFE0F) ||
                (next_cp >= 0x1F3FB && next_cp <= 0x1F3FF);
            if (modifier) {
                current += next_consumed;
                remaining -= next_consumed;
                continue;
            }

            // ZWJ → consume, then consume the next codepoint as joined.
            if (next_cp != 0x200D) break;
            joined_emoji = true;
            current += next_consumed;
            remaining -= next_consumed;

            if (remaining == 0) break;

            const uint32_t joined_decoded = utf8Decode(current, remaining);
            const std::size_t joined_consumed = joined_decoded & 0xFF;
            if (joined_consumed == 0 || joined_consumed > remaining) break;
            current += joined_consumed;
            remaining -= joined_consumed;
        }

        width += (joined_emoji || regional_pair)
            ? 2
            : (cw > 0 ? static_cast<std::size_t>(cw) : 0);
    }
    return width;
}

std::string formatMarkdownTables(
    const std::string& text,
    std::size_t max_width
) {
    std::vector<std::string> lines;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }

    std::ostringstream output;
    for (std::size_t index = 0; index < lines.size();) {
        if (index + 1 < lines.size() &&
            lines[index].find('|') != std::string::npos &&
            isSeparatorRow(lines[index + 1])) {
            std::vector<std::string> table = {lines[index], lines[index + 1]};
            index += 2;
            while (index < lines.size() &&
                   lines[index].find('|') != std::string::npos &&
                   !trim(lines[index]).empty()) {
                table.push_back(lines[index++]);
            }
            output << renderTable(table, max_width);
        } else {
            output << lines[index++];
        }
        if (index < lines.size()) {
            output << '\n';
        }
    }
    if (!text.empty() && text.back() == '\n') {
        output << '\n';
    }
    return output.str();
}

} // namespace opencode
