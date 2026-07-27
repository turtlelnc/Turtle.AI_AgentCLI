#include "diff_view.hpp"

#include <sstream>

namespace opencode {

DiffDocument DiffDocument::parse(const std::string& unified_diff) {
    DiffDocument document;
    std::istringstream input(unified_diff);
    std::string line;
    DiffFile* current = nullptr;
    while (std::getline(input, line)) {
        if (line.rfind("diff --git ", 0) == 0) {
            document.files_.push_back({});
            current = &document.files_.back();
            const auto marker = line.find(" b/");
            current->path = marker == std::string::npos
                ? line.substr(11) : line.substr(marker + 3);
        } else if (!current &&
                   (line.rfind("--- ", 0) == 0 ||
                    line.rfind("+++ ", 0) == 0)) {
            document.files_.push_back({});
            current = &document.files_.back();
        }
        if (!current) continue;
        current->content += line + '\n';
        if (line.rfind("+++ ", 0) == 0) {
            std::string path = line.substr(4);
            if (path.rfind("b/", 0) == 0) path.erase(0, 2);
            if (path != "/dev/null") current->path = path;
        } else if (!line.empty() && line.front() == '+' &&
                   line.rfind("+++", 0) != 0) {
            ++current->additions;
        } else if (!line.empty() && line.front() == '-' &&
                   line.rfind("---", 0) != 0) {
            ++current->deletions;
        }
    }
    if (document.files_.size() == 1 && document.files_[0].path.empty()) {
        document.files_[0].path = "file";
    }
    return document;
}

} // namespace opencode
