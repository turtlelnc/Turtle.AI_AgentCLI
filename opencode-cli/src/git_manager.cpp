#include "git_manager.hpp"
#include <array>
#include <memory>
#include <cstdio>

namespace opencode {

GitManager::GitManager() {}

bool GitManager::is_git_installed() {
    return execute_cmd("which git", ".") != "";
}

std::string GitManager::execute_cmd(const std::string& cmd, const std::string& work_dir) {
    std::array<char, 256> buffer;
    std::string result;
    std::string full_cmd = "cd \"" + work_dir + "\" && " + cmd + " 2>&1";
    
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(full_cmd.c_str(), "r"), pclose);
    if (!pipe) {
        return "";
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    
    return result;
}

GitStatus GitManager::check_status(const std::string& work_dir) {
    GitStatus status;
    
    // Check if it's a git repo
    std::string rev_parse = execute_cmd("git rev-parse --is-inside-work-tree", work_dir);
    status.is_repo = (rev_parse.find("true") != std::string::npos);
    
    if (!status.is_repo) {
        return status;
    }
    
    // Check for uncommitted changes
    std::string git_status = execute_cmd("git status --porcelain", work_dir);
    status.has_uncommitted = !git_status.empty();
    
    // Parse modified and untracked files
    if (!git_status.empty()) {
        size_t pos = 0;
        while ((pos = git_status.find('\n')) != std::string::npos) {
            std::string line = git_status.substr(0, pos);
            if (line.length() >= 3) {
                std::string file = line.substr(3);
                if (line[0] == '?') {
                    status.untracked_files.push_back(file);
                } else {
                    status.modified_files.push_back(file);
                }
            }
            git_status.erase(0, pos + 1);
        }
    }
    
    // Check if behind remote
    std::string fetch = execute_cmd("git fetch origin", work_dir);
    std::string branch = get_current_branch(work_dir);
    if (!branch.empty()) {
        std::string behind = execute_cmd("git rev-list --count HEAD..origin/" + branch, work_dir);
        try {
            status.commits_behind = std::stoi(behind);
            status.behind_remote = (status.commits_behind > 0);
        } catch (...) {
            status.commits_behind = 0;
        }
    }
    
    return status;
}

bool GitManager::init_repo(const std::string& work_dir) {
    std::string result = execute_cmd("git init", work_dir);
    return result.find("Initialized") != std::string::npos;
}

bool GitManager::stash_changes(const std::string& work_dir) {
    std::string result = execute_cmd("git stash push -m \"opencode-stash\"", work_dir);
    return result.find("Saved") != std::string::npos || result.empty();
}

bool GitManager::pop_stash(const std::string& work_dir) {
    std::string result = execute_cmd("git stash pop", work_dir);
    return result.find("Dropped") != std::string::npos || result.empty();
}

bool GitManager::pull_remote(const std::string& work_dir) {
    std::string result = execute_cmd("git pull --rebase", work_dir);
    return result.find("Already up to date") != std::string::npos || 
           result.find("Fast-forward") != std::string::npos ||
           result.find("From") != std::string::npos;
}

std::string GitManager::get_current_branch(const std::string& work_dir) {
    std::string result = execute_cmd("git rev-parse --abbrev-ref HEAD", work_dir);
    // Remove newline
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    return result;
}

} // namespace opencode
