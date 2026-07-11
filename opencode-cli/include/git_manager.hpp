#ifndef GIT_MANAGER_HPP
#define GIT_MANAGER_HPP

#include <string>
#include <vector>

namespace opencode {

struct GitStatus {
    bool is_repo{false};
    bool has_uncommitted{false};
    bool behind_remote{false};
    int commits_behind{0};
    std::vector<std::string> modified_files;
    std::vector<std::string> untracked_files;
};

class GitManager {
public:
    GitManager();
    
    GitStatus check_status(const std::string& work_dir);
    bool init_repo(const std::string& work_dir);
    bool stash_changes(const std::string& work_dir);
    bool pop_stash(const std::string& work_dir);
    bool pull_remote(const std::string& work_dir);
    std::string get_current_branch(const std::string& work_dir);
    
private:
    std::string execute_cmd(const std::string& cmd, const std::string& work_dir);
    bool is_git_installed();
};

} // namespace opencode

#endif // GIT_MANAGER_HPP
