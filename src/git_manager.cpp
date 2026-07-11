#include "git_manager.hpp"
#include <cstdlib>
#include <array>
#include <sstream>

namespace opencode {

GitManager::GitManager() {}

std::string GitManager::executeGitCommand(const std::string& cmd, const std::string& path) {
    std::string full_cmd = "cd \"" + path + "\" && " + cmd + " 2>&1";
    std::array<char, 128> buffer;
    std::string result;
    
    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) {
        return "";
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    
    pclose(pipe);
    return result;
}

bool GitManager::isGitAvailable() {
    std::array<char, 128> buffer;
    FILE* pipe = popen("git --version 2>&1", "r");
    if (!pipe) {
        return false;
    }
    
    bool found = (fgets(buffer.data(), buffer.size(), pipe) != nullptr);
    pclose(pipe);
    return found;
}

GitStatus GitManager::checkStatus(const std::string& path) {
    GitStatus status;
    status.is_repo = false;
    status.has_uncommitted_changes = false;
    status.is_ahead_remote = false;
    status.is_behind_remote = false;
    status.commits_behind = 0;
    status.commits_ahead = 0;
    
    // 检查是否为 git 仓库
    std::string rev_parse = executeGitCommand("git rev-parse --is-inside-work-tree", path);
    if (rev_parse.find("true") == std::string::npos) {
        return status;
    }
    status.is_repo = true;
    
    // 获取当前分支
    std::string branch = executeGitCommand("git rev-parse --abbrev-ref HEAD", path);
    if (!branch.empty()) {
        branch.erase(branch.find_last_not_of("\n\r") + 1);
        status.current_branch = branch;
    }
    
    // 检查未提交改动
    std::string diff = executeGitCommand("git status --porcelain", path);
    if (!diff.empty()) {
        status.has_uncommitted_changes = true;
        std::istringstream iss(diff);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.length() > 3) {
                status.modified_files.push_back(line.substr(3));
            }
        }
    }
    
    // 检查远程同步状态
    std::string fetch = executeGitCommand("git fetch origin 2>&1", path);
    std::string ahead_behind = executeGitCommand("git rev-list --left-right --count HEAD...origin/" + status.current_branch, path);
    
    std::istringstream iss(ahead_behind);
    int ahead, behind;
    if (iss >> ahead >> behind) {
        status.commits_ahead = ahead;
        status.commits_behind = behind;
        status.is_ahead_remote = (ahead > 0);
        status.is_behind_remote = (behind > 0);
    }
    
    return status;
}

std::vector<std::string> GitManager::getUncommittedFiles(const std::string& path) {
    std::vector<std::string> files;
    std::string diff = executeGitCommand("git status --porcelain", path);
    
    std::istringstream iss(diff);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.length() > 3) {
            files.push_back(line.substr(3));
        }
    }
    
    return files;
}

int GitManager::checkBehindRemote(const std::string& path) {
    std::string fetch = executeGitCommand("git fetch origin 2>&1", path);
    std::string branch = executeGitCommand("git rev-parse --abbrev-ref HEAD", path);
    branch.erase(branch.find_last_not_of("\n\r") + 1);
    
    std::string ahead_behind = executeGitCommand("git rev-list --left-right --count HEAD...origin/" + branch, path);
    
    std::istringstream iss(ahead_behind);
    int ahead, behind;
    if (iss >> ahead >> behind) {
        return behind;
    }
    
    return 0;
}

bool GitManager::pull(const std::string& path) {
    std::string result = executeGitCommand("git pull --rebase", path);
    return (result.find("error") == std::string::npos && result.find("fatal") == std::string::npos);
}

bool GitManager::stash(const std::string& path) {
    std::string result = executeGitCommand("git stash push -m \"opencode-auto-stash\"", path);
    return (result.find("error") == std::string::npos && result.find("fatal") == std::string::npos);
}

bool GitManager::stashPop(const std::string& path) {
    std::string result = executeGitCommand("git stash pop", path);
    return (result.find("error") == std::string::npos && result.find("fatal") == std::string::npos);
}

bool GitManager::checkout(const std::string& path) {
    std::string result = executeGitCommand("git checkout .", path);
    return (result.find("error") == std::string::npos && result.find("fatal") == std::string::npos);
}

} // namespace opencode
