#ifndef TOOL_REGISTRY_HPP
#define TOOL_REGISTRY_HPP

#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <optional>
#include <nlohmann/json.hpp>
#include "change_journal.hpp"
#include "cancellation.hpp"

namespace opencode {

struct RegisteredTool {
    std::string name;
    std::string description;
    std::function<nlohmann::json(const nlohmann::json&)> handler;
    nlohmann::json parameters = nullptr;
};

class ToolRegistry {
public:
    ToolRegistry();

    // 将所有文件工具限制在指定工作区内
    bool setWorkspaceRoot(const std::string& path);
    void setChangeJournal(ChangeJournal* journal);
    void setCancellationToken(CancellationToken* token);

    // 注册内置工具
    void registerBuiltinTools();

    // 注册自定义工具
    void registerTool(const RegisteredTool& tool);

    // 获取所有工具列表 (用于 AI 调用)
    std::vector<nlohmann::json> getToolsSchema() const;

    // 执行工具
    nlohmann::json executeTool(const std::string& name, const nlohmann::json& args);

    // 检查工具是否存在
    bool hasTool(const std::string& name) const;

private:
    std::vector<RegisteredTool> tools_;
    std::filesystem::path workspace_root_;
    ChangeJournal* change_journal_ = nullptr;
    CancellationToken* cancellation_token_ = nullptr;

    std::optional<std::filesystem::path> resolveWorkspacePath(
        const std::string& path,
        bool target_may_not_exist = false
    ) const;

    // 内置工具实现
    nlohmann::json readFile(const nlohmann::json& args);
    nlohmann::json writeFile(const nlohmann::json& args);
    nlohmann::json editFile(const nlohmann::json& args);
    nlohmann::json listDir(const nlohmann::json& args);
    nlohmann::json runTerminal(const nlohmann::json& args);
    nlohmann::json runShell(const nlohmann::json& args);
};

} // namespace opencode

#endif // TOOL_REGISTRY_HPP
