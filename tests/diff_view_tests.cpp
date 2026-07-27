#include "diff_view.hpp"
#include <iostream>

int main() {
    const std::string diff =
        "diff --git a/a.cpp b/a.cpp\n"
        "--- a/a.cpp\n+++ b/a.cpp\n"
        "@@ -1,2 +1,3 @@\n-old\n+new\n+more\n keep\n"
        "diff --git a/b.md b/b.md\n"
        "--- a/b.md\n+++ b/b.md\n"
        "@@ -1 +0,0 @@\n-removed\n";
    const auto document = opencode::DiffDocument::parse(diff);
    if (document.files().size() != 2 ||
        document.files()[0].path != "a.cpp" ||
        document.files()[0].additions != 2 ||
        document.files()[0].deletions != 1 ||
        document.files()[1].path != "b.md" ||
        document.files()[1].additions != 0 ||
        document.files()[1].deletions != 1 ||
        document.files()[0].expanded) {
        std::cerr << "FAILED: unified diff summary parsing\n";
        return 1;
    }
    std::cout << "Diff view checks passed\n";
    return 0;
}
