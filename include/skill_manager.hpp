#ifndef SKILL_MANAGER_HPP
#define SKILL_MANAGER_HPP

#include <filesystem>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "change_journal.hpp"

namespace opencode {

struct SkillMetadata {
    std::string name;
    std::string description;
    std::filesystem::path file_path;
};

class SkillManager {
public:
    void discover(const std::vector<std::filesystem::path>& roots);
    nlohmann::json listSkills() const;
    nlohmann::json loadSkill(const std::string& name) const;
    nlohmann::json manageSkill(
        const nlohmann::json& args,
        const std::filesystem::path& workspace_root,
        ChangeJournal& journal
    );
    std::string getCatalogPrompt() const;
    std::size_t size() const;

private:
    std::vector<SkillMetadata> skills_;
};

} // namespace opencode

#endif // SKILL_MANAGER_HPP
