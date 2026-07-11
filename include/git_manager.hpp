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
    
    // 检查目录是否为 git 仓库
    GitStatus checkStatus(const std::string& path);
    
    // 获取未提交的文件列表
    std::vector<std::string> getUncommittedFiles(const std::string& path);
    
    // 检查是否落后远程
    int checkBehindRemote(const std::string& path);
    
    // 执行 git pull
    bool pull(const std::string& path);
    
    // 执行 git stash
    bool stash(const std::string& path);
    
    // 执行 git stash pop
    bool stashPop(const std::string& path);
    
    // 执行 git checkout 撤销改动
    bool checkout(const std::string& path);
    
    // 检查 git 是否可用
    static bool isGitAvailable();
    
private:
    std::string executeGitCommand(const std::string& cmd, const std::string& path);
};

} // namespace opencode

#endif // GIT_MANAGER_HPP
