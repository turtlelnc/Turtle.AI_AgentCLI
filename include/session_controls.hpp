#ifndef SESSION_CONTROLS_HPP
#define SESSION_CONTROLS_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace opencode {

struct Subtask {
    std::string text;
    bool completed = false;
};

class MemoryStore {
public:
    explicit MemoryStore(std::filesystem::path workspace);
    bool appendGlobal(const std::string& section, const std::string& text);
    bool appendProject(const std::string& section, const std::string& text);
    bool rememberProject();
    std::string promptContext(std::size_t max_bytes = 16384) const;
    std::string display() const;

private:
    std::filesystem::path global_path_;
    std::filesystem::path project_path_;
    static bool append(
        const std::filesystem::path& path,
        const std::string& section,
        const std::string& text
    );
    static std::string readBounded(
        const std::filesystem::path& path,
        std::size_t max_bytes
    );
};

class GoalState {
public:
    bool start(const std::string& text);
    void complete();
    bool active() const { return !goal_.empty(); }
    const std::string& text() const { return goal_; }
    bool addSubtask(const std::string& text);
    bool completeSubtask(std::size_t one_based_index);
    const std::vector<Subtask>& subtasks() const { return subtasks_; }
    std::size_t completedSubtasks() const;
    std::string prompt() const;

private:
    std::string goal_;
    std::vector<Subtask> subtasks_;
};

class KeepAwakeGuard {
public:
    KeepAwakeGuard();
    ~KeepAwakeGuard();
    bool start();
    void stop();
    bool active() const;

private:
#if defined(_WIN32)
    bool active_ = false;
#else
    int child_pid_ = -1;
#endif
};

} // namespace opencode

#endif
