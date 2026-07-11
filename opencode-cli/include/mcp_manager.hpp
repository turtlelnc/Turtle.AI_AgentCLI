#ifndef MCP_MANAGER_HPP
#define MCP_MANAGER_HPP

#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

namespace opencode {

struct McpTool {
    std::string name;
    std::string description;
    std::string schema;  // JSON Schema for parameters
    std::function<std::string(const nlohmann::json&)> handler;
};

class McpManager {
public:
    McpManager();
    
    void register_tool(const McpTool& tool);
    std::vector<std::string> list_tools() const;
    std::string execute_tool(const std::string& name, const nlohmann::json& params);
    
    // Built-in tools
    void register_builtin_tools();
    
private:
    std::vector<McpTool> tools_;
    
    // Built-in tool handlers
    std::string handle_read_file(const nlohmann::json& params);
    std::string handle_list_dir(const nlohmann::json& params);
    std::string handle_search_files(const nlohmann::json& params);
    std::string handle_write_file(const nlohmann::json& params);      // NEW
    std::string handle_run_command(const nlohmann::json& params);     // NEW
};

} // namespace opencode

#endif // MCP_MANAGER_HPP
