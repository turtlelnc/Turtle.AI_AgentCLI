#include "skill_manager.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

void writeSkill(
    const std::filesystem::path& root,
    const std::string& name,
    const std::string& description,
    const std::string& body
) {
    std::filesystem::create_directories(root / name);
    std::ofstream file(root / name / "SKILL.md");
    file << "---\nname: " << name
         << "\ndescription: " << description
         << "\n---\n\n" << body << '\n';
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    const fs::path root = fs::temp_directory_path() / ("turtle-skills-" + unique);
    const fs::path bundled = root / "bundled";
    const fs::path workspace = root / "workspace";

    writeSkill(bundled, "review-code", "Bundled review workflow", "bundled body");
    writeSkill(workspace, "review-code", "Workspace review workflow", "workspace body");
    writeSkill(workspace, "debug-issue", "Debug failures", "debug body");
    writeSkill(workspace, "Invalid_Name", "Must be ignored", "invalid");

    opencode::SkillManager manager;
    manager.discover({bundled, workspace});
    bool passed = expect(manager.size() == 2, "valid skills are discovered");

    const auto listed = manager.listSkills();
    passed &= expect(listed["skills"].size() == 2, "skill metadata is listed");

    const auto review = manager.loadSkill("review-code");
    passed &= expect(review.value("success", false), "skill body loads");
    passed &= expect(
        review.value("content", "").find("workspace body") != std::string::npos,
        "later workspace root overrides bundled skill"
    );
    passed &= expect(
        review.value("content", "").find("Mandatory Execution Loop") != std::string::npos &&
            review.value("content", "").find("verification fails") != std::string::npos,
        "every loaded skill requires a complete execution and verification loop"
    );

    const std::string catalog = manager.getCatalogPrompt();
    passed &= expect(
        catalog.find("$debug-issue") != std::string::npos &&
        catalog.find("load_skill") != std::string::npos &&
        catalog.find("Never stop at planning") != std::string::npos,
        "catalog teaches progressive skill loading"
    );
    passed &= expect(
        !manager.loadSkill("missing").value("success", true),
        "missing skill fails clearly"
    );

    opencode::ChangeJournal journal;
    const auto managed = manager.manageSkill({
        {"action", "upsert"},
        {"name", "new-workflow"},
        {"content", "---\nname: new-workflow\ndescription: A managed workflow\n---\n\nDo it.\n"}
    }, root, journal);
    passed &= expect(
        managed.value("success", false) &&
            fs::exists(root / ".turtle/skills/new-workflow/SKILL.md"),
        "AI can create a validated workspace skill"
    );
    const auto deleted = manager.manageSkill({
        {"action", "delete"}, {"name", "new-workflow"}
    }, root, journal);
    passed &= expect(
        deleted.value("success", false) &&
            !fs::exists(root / ".turtle/skills/new-workflow"),
        "AI can delete a workspace skill"
    );
    passed &= expect(
        journal.undo().value("success", false) &&
            fs::exists(root / ".turtle/skills/new-workflow/SKILL.md"),
        "deleted skill can be restored by a human undo"
    );

    std::error_code error;
    fs::remove_all(root, error);
    return passed ? 0 : 1;
}
