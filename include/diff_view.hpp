#ifndef DIFF_VIEW_HPP
#define DIFF_VIEW_HPP

#include <string>
#include <vector>

namespace opencode {

struct DiffFile {
    std::string path;
    std::string content;
    std::size_t additions = 0;
    std::size_t deletions = 0;
    bool expanded = false;
};

class DiffDocument {
public:
    static DiffDocument parse(const std::string& unified_diff);
    const std::vector<DiffFile>& files() const { return files_; }
    std::vector<DiffFile>& files() { return files_; }
    bool empty() const { return files_.empty(); }

private:
    std::vector<DiffFile> files_;
};

} // namespace opencode

#endif
