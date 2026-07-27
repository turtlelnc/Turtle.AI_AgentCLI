#include "change_journal.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

std::string read(const std::filesystem::path& path) {
    std::ifstream input(path);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
}

void write(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << content;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    const fs::path root = fs::temp_directory_path() / ("turtle-undo-" + unique);
    const fs::path file = root / "file.txt";
    write(file, "original");

    opencode::ChangeJournal journal;
    auto first = journal.capture(file);
    write(file, "first");
    const auto first_id = journal.record("edit_file", file, std::move(first));

    auto second = journal.capture(file);
    write(file, "second");
    const auto second_id = journal.record("edit_file", file, std::move(second));

    bool passed = expect(
        !journal.undo(first_id).value("success", true),
        "older overlapping change cannot be undone first"
    );
    passed &= expect(
        journal.undo(second_id).value("success", false) && read(file) == "first",
        "latest edit restores its preimage"
    );
    passed &= expect(
        journal.undo(first_id).value("success", false) && read(file) == "original",
        "earlier edit becomes safely undoable"
    );

    const fs::path created = root / "new.txt";
    auto missing = journal.capture(created);
    write(created, "new");
    journal.record("write_file", created, std::move(missing));
    passed &= expect(
        journal.undo().value("success", false) && !fs::exists(created),
        "undo removes a newly created file"
    );

    std::error_code error;
    fs::remove_all(root, error);
    return passed ? 0 : 1;
}
