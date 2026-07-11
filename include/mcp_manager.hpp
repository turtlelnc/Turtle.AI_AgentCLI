#ifndef MCP_MANAGER_HPP
#define MCP_MANAGER_HPP

#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

namespace opencode {

struct MCPTool {
    std::string name;
    std::string description;
    std::function<nlohmann::json(const nlohmann::json&)> handler;
};

class MCPManager {
public:
    MCPManager();
    
    // 注册内置工具
    void registerBuiltinTools();
    
    // 注册自定义工具
    void registerTool(const MCPTool& tool);
    
    // 获取所有工具列表 (用于 AI 调用)
    std::vector<nlohmann::json> getToolsSchema() const;
    
    // 执行工具
    nlohmann::json executeTool(const std::string& name, const nlohmann::json& args);
    
    // 检查工具是否存在
    bool hasTool(const std::string& name) const;
    
private:
    std::vector<MCPTool> tools_;
    
    // 内置工具实现
    nlohmann::json readFile(const nlohmann::json& args);
    nlohmann::json writeFile(const nlohmann::json& args);
    nlohmann::json listDir(const nlohmann::json& args);
    nlohmann::json runCommand(const nlohmann::json& args);
};

} // namespace opencode

#endif // MCP_MANAGER_HPP
