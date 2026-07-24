#include "git_manager.hpp"
#include <cstdlib>
#include <array>
#include <sstream>
#include <iostream>

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
    
    // Check if it's a git repository
    std::string rev_parse = executeGitCommand("git rev-parse --is-inside-work-tree", path);
    if (rev_parse.find("true") == std::string::npos) {
        return status;
    }
    status.is_repo = true;
    
    // Get current branch
    std::string branch = executeGitCommand("git rev-parse --abbrev-ref HEAD", path);
    if (!branch.empty()) {
        branch.erase(branch.find_last_not_of("\n\r") + 1);
        status.current_branch = branch;
    }
    
    // Check uncommitted changes
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
    
    // Check remote sync status
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

// Request user confirmation for dangerous Git operations
bool GitManager::confirmDangerousOperation(const std::string& operation) {
    std::cout << "\n⚠️  WARNING: About to execute dangerous Git operation: " << operation << std::endl;
    std::cout << "   This may result in permanent data loss." << std::endl;
    std::cout << "   Are you sure? (yes/no): ";
    
    std::string response;
    std::cin >> response;
    
    return response == "yes";
}

bool GitManager::pull(const std::string& path) {
    // Check for uncommitted changes before pull
    GitStatus status = checkStatus(path);
    if (status.has_uncommitted_changes) {
        std::cout << "\n⚠️  You have uncommitted changes. Pull may cause conflicts." << std::endl;
        std::cout << "   Recommended: Commit or stash changes first." << std::endl;
        if (!confirmDangerousOperation("git pull with uncommitted changes")) {
            return false;
        }
    }
    
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
    // Checkout is dangerous as it discards local changes
    GitStatus status = checkStatus(path);
    if (status.has_uncommitted_changes) {
        std::cout << "\n⚠️  WARNING: git checkout will discard all uncommitted changes!" << std::endl;
        std::cout << "   Modified files: ";
        for (const auto& file : status.modified_files) {
            std::cout << file << " ";
        }
        std::cout << std::endl;
        
        if (!confirmDangerousOperation("git checkout (discards changes)")) {
            return false;
        }
    }
    
    std::string result = executeGitCommand("git checkout .", path);
    return (result.find("error") == std::string::npos && result.find("fatal") == std::string::npos);
}

} // namespace opencode
