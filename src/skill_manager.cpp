#include "skill_manager.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

namespace opencode {

namespace {

constexpr std::size_t kMaxSkillBytes = 65536;
constexpr std::size_t kMaxSkills = 64;

const char* kSkillExecutionContract = R"(# Mandatory Execution Loop

Treat this skill as an end-to-end workflow, not as advice.

1. Define the requested outcome and observable completion criteria.
2. Inspect the relevant state and gather enough evidence to act.
3. Execute the required in-scope work with tools when the request authorizes changes.
4. Verify the result against the completion criteria using tests, builds, or direct inspection.
5. If verification fails, diagnose the evidence, make a safe correction, and verify again.
6. Stop only when the outcome is verified, the user denies a required action, or a concrete blocker prevents further progress.
7. Report what changed, verification evidence, and any remaining limitation.

Do not stop after a plan, a single tool call, or an unverified edit. Do not claim success when verification was skipped or failed.

)";

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string unquote(std::string value) {
    value = trim(value);
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

bool validSkillName(const std::string& name) {
    if (name.empty() || name.size() > 63) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') || c == '-';
    });
}

bool parseMetadata(
    const std::filesystem::path& file_path,
    SkillMetadata& metadata
) {
    std::ifstream file(file_path);
    if (!file) return false;

    std::string line;
    if (!std::getline(file, line) || trim(line) != "---") return false;
    bool closed = false;
    while (std::getline(file, line)) {
        if (trim(line) == "---") {
            closed = true;
            break;
        }
        const auto separator = line.find(':');
        if (separator == std::string::npos) continue;
        const std::string key = trim(line.substr(0, separator));
        const std::string value = unquote(line.substr(separator + 1));
        if (key == "name") metadata.name = value;
        else if (key == "description") metadata.description = value;
    }
    metadata.file_path = file_path;
    return closed && validSkillName(metadata.name) &&
           !metadata.description.empty() && metadata.description.size() <= 1024;
}

} // namespace

void SkillManager::discover(const std::vector<std::filesystem::path>& roots) {
    std::map<std::string, SkillMetadata> discovered;
    for (const auto& root : roots) {
        std::error_code error;
        if (!std::filesystem::is_directory(root, error) || error) continue;
        for (std::filesystem::directory_iterator it(root, error), end;
             !error && it != end; it.increment(error)) {
            if (!it->is_directory(error) || error) continue;
            SkillMetadata metadata;
            if (parseMetadata(it->path() / "SKILL.md", metadata)) {
                // Later roots have higher priority, allowing workspace skills
                // to override bundled and user-level skills by name.
                discovered[metadata.name] = std::move(metadata);
            }
        }
    }

    skills_.clear();
    for (auto& [name, metadata] : discovered) {
        if (skills_.size() >= kMaxSkills) break;
        skills_.push_back(std::move(metadata));
    }
}

nlohmann::json SkillManager::listSkills() const {
    nlohmann::json skills = nlohmann::json::array();
    for (const auto& skill : skills_) {
        skills.push_back({
            {"name", skill.name},
            {"description", skill.description}
        });
    }
    return {{"success", true}, {"skills", skills}};
}

nlohmann::json SkillManager::loadSkill(const std::string& name) const {
    const auto it = std::find_if(
        skills_.begin(), skills_.end(),
        [&](const SkillMetadata& skill) { return skill.name == name; }
    );
    if (it == skills_.end()) {
        return {{"success", false}, {"error", "Skill not found: " + name}};
    }

    std::ifstream file(it->file_path, std::ios::binary);
    if (!file) {
        return {{"success", false}, {"error", "Cannot read skill: " + name}};
    }
    std::string content(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );
    if (content.size() > kMaxSkillBytes) {
        return {{"success", false}, {"error", "Skill exceeds 64 KiB limit: " + name}};
    }
    return {
        {"success", true},
        {"name", name},
        {"content", std::string(kSkillExecutionContract) + content}
    };
}

nlohmann::json SkillManager::manageSkill(
    const nlohmann::json& args,
    const std::filesystem::path& workspace_root,
    ChangeJournal& journal
) {
    const std::string action = args.value("action", "");
    const std::string name = args.value("name", "");
    if (!validSkillName(name)) {
        return {{"success", false}, {"error", "Invalid skill name"}};
    }
    const auto skill_root = workspace_root / ".turtle" / "skills" / name;
    const auto skill_file = skill_root / "SKILL.md";
    auto before = journal.capture(skill_root);

    std::error_code error;
    if (action == "delete") {
        if (!before.existed) {
            return {{"success", false}, {"error", "Workspace skill does not exist: " + name}};
        }
        std::filesystem::remove_all(skill_root, error);
        if (error) return {{"success", false}, {"error", error.message()}};
    } else if (action == "upsert") {
        if (!args.contains("content") || !args["content"].is_string()) {
            return {{"success", false}, {"error", "Missing string argument: content"}};
        }
        const std::string content = args["content"].get<std::string>();
        if (content.size() > kMaxSkillBytes) {
            return {{"success", false}, {"error", "Skill exceeds 64 KiB limit"}};
        }
        const auto temporary = skill_root.parent_path() / (name + ".tmp");
        std::filesystem::remove_all(temporary, error);
        error.clear();
        std::filesystem::create_directories(temporary, error);
        if (error) return {{"success", false}, {"error", error.message()}};
        {
            std::ofstream output(temporary / "SKILL.md", std::ios::binary);
            output << content;
        }
        SkillMetadata metadata;
        if (!parseMetadata(temporary / "SKILL.md", metadata) || metadata.name != name) {
            std::filesystem::remove_all(temporary, error);
            return {
                {"success", false},
                {"error", "SKILL.md needs valid matching name and non-empty description"}
            };
        }
        std::filesystem::create_directories(skill_root, error);
        if (!error) {
            std::filesystem::rename(
                temporary / "SKILL.md", skill_file, error
            );
        }
        std::error_code cleanup_error;
        std::filesystem::remove_all(temporary, cleanup_error);
        if (error) return {{"success", false}, {"error", error.message()}};
    } else {
        return {{"success", false}, {"error", "action must be 'upsert' or 'delete'"}};
    }

    const auto change_id =
        journal.record("skill_" + action, skill_root, std::move(before));
    return {
        {"success", true},
        {"message", action == "delete" ? "Workspace skill deleted" : "Workspace skill saved"},
        {"name", name},
        {"path", skill_file.string()},
        {"change_id", change_id}
    };
}

std::string SkillManager::getCatalogPrompt() const {
    if (skills_.empty()) return "";
    std::ostringstream prompt;
    prompt << "\n\n# Available Skills\n"
           << "Skills contain task-specific workflows. When a request explicitly names "
           << "`$skill-name`, or clearly matches a description below, call `load_skill` "
           << "before acting and follow the loaded instructions. Every loaded skill has a "
           << "mandatory execution loop: continue from inspection through execution, "
           << "verification, and correction until the requested outcome is verified or a "
           << "concrete blocker is reached. Never stop at planning or after an unverified edit. "
           << "Do not load unrelated skills. "
           << "When the user explicitly asks to create, change, or delete a skill, use "
           << "`manage_skill`; it writes only workspace-local skills and requires approval.\n\n";
    for (const auto& skill : skills_) {
        prompt << "- $" << skill.name << ": " << skill.description << '\n';
    }
    return prompt.str();
}

std::size_t SkillManager::size() const {
    return skills_.size();
}

} // namespace opencode
