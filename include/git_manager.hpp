#ifndef GIT_MANAGER_HPP
#define GIT_MANAGER_HPP

#include <string>
#include <vector>

namespace opencode {

struct GitStatus {
    bool is_repo;
    bool has_uncommitted_changes;
    bool is_ahead_remote;
    bool is_behind_remote;
    int commits_behind;
    int commits_ahead;
    std::vector<std::string> modified_files;
    std::string current_branch;
};

class GitManager {
public:
    GitManager();
    
    // Check if directory is a git repository
    GitStatus checkStatus(const std::string& path);
    
    // Get list of uncommitted files
    std::vector<std::string> getUncommittedFiles(const std::string& path);
    
    // Check if behind remote
    int checkBehindRemote(const std::string& path);
    
    // Execute git pull (with confirmation for dangerous operations)
    bool pull(const std::string& path);
    
    // Execute git stash
    bool stash(const std::string& path);
    
    // Execute git stash pop
    bool stashPop(const std::string& path);
    
    // Execute git checkout to discard changes (with confirmation)
    bool checkout(const std::string& path);
    
    // Check if git is available
    static bool isGitAvailable();
    
    // Request user confirmation for dangerous operations
    bool confirmDangerousOperation(const std::string& operation);
    
private:
    std::string executeGitCommand(const std::string& cmd, const std::string& path);
};

} // namespace opencode

#endif // GIT_MANAGER_HPP
