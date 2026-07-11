#include "mcp_manager.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <array>
#include <chrono>
#include <future>

namespace opencode {

MCPManager::MCPManager() {
    registerBuiltinTools();
}

void MCPManager::registerBuiltinTools() {
    // 文件读取工具
    tools_.push_back({
        "read_file",
        "Read contents of a file (max 10KB)",
        [this](const nlohmann::json& args) { return readFile(args); }
    });
    
    // 文件写入工具
    tools_.push_back({
        "write_file",
        "Write content to a file (with safety checks)",
        [this](const nlohmann::json& args) { return writeFile(args); }
    });
    
    // 目录列表工具
    tools_.push_back({
        "list_directory",
        "List files in a directory",
        [this](const nlohmann::json& args) { return listDir(args); }
    });
    
    // 安全命令执行工具
    tools_.push_back({
        "run_command",
        "Run a shell command (safe subset only)",
        [this](const nlohmann::json& args) { return runCommand(args); }
    });
}

void MCPManager::registerTool(const MCPTool& tool) {
    tools_.push_back(tool);
}

std::vector<nlohmann::json> MCPManager::getToolsSchema() const {
    std::vector<nlohmann::json> schema;
    
    for (const auto& tool : tools_) {
        nlohmann::json s;
        s["type"] = "function";
        s["function"]["name"] = tool.name;
        s["function"]["description"] = tool.description;
        s["function"]["parameters"]["type"] = "object";
        
        if (tool.name == "read_file" || tool.name == "write_file") {
            s["function"]["parameters"]["properties"]["path"]["type"] = "string";
            s["function"]["parameters"]["properties"]["path"]["description"] = "File path";
            s["function"]["parameters"]["required"] = nlohmann::json::array({"path"});
            
            if (tool.name == "write_file") {
                s["function"]["parameters"]["properties"]["content"]["type"] = "string";
                s["function"]["parameters"]["properties"]["content"]["description"] = "File content";
                s["function"]["parameters"]["required"].push_back("content");
            }
        } else if (tool.name == "list_directory") {
            s["function"]["parameters"]["properties"]["path"]["type"] = "string";
            s["function"]["parameters"]["properties"]["path"]["description"] = "Directory path";
            s["function"]["parameters"]["required"] = nlohmann::json::array({"path"});
        } else if (tool.name == "run_command") {
            s["function"]["parameters"]["properties"]["command"]["type"] = "string";
            s["function"]["parameters"]["properties"]["command"]["description"] = "Shell command (limited to safe commands)";
            s["function"]["parameters"]["required"] = nlohmann::json::array({"command"});
        }
        
        schema.push_back(s);
    }
    
    return schema;
}

nlohmann::json MCPManager::executeTool(const std::string& name, const nlohmann::json& args) {
    for (const auto& tool : tools_) {
        if (tool.name == name) {
            try {
                return tool.handler(args);
            } catch (const std::exception& e) {
                return {{"success", false}, {"error", e.what()}};
            }
        }
    }
    return {{"success", false}, {"error", "Tool not found: " + name}};
}

bool MCPManager::hasTool(const std::string& name) const {
    for (const auto& tool : tools_) {
        if (tool.name == name) return true;
    }
    return false;
}

nlohmann::json MCPManager::readFile(const nlohmann::json& args) {
    if (!args.contains("path")) {
        return {{"success", false}, {"error", "Missing 'path' argument"}};
    }
    
    std::string path = args["path"];
    
    // 安全检查：防止路径遍历攻击
    if (path.find("..") != std::string::npos) {
        return {{"success", false}, {"error", "Invalid path: contains '..'"}};
    }
    
    std::ifstream file(path);
    if (!file.is_open()) {
        return {{"success", false}, {"error", "Cannot open file: " + path}};
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    // 限制文件大小 (10KB)
    if (content.size() > 10240) {
        content = content.substr(0, 10240) + "\n... [truncated]";
    }
    
    return {{"success", true}, {"content", content}, {"size", static_cast<int64_t>(content.size())}};
}

nlohmann::json MCPManager::writeFile(const nlohmann::json& args) {
    if (!args.contains("path") || !args.contains("content")) {
        return {{"success", false}, {"error", "Missing 'path' or 'content' argument"}};
    }
    
    std::string path = args["path"];
    std::string content = args["content"];
    
    // 安全检查
    if (path.find("..") != std::string::npos) {
        return {{"success", false}, {"error", "Invalid path: contains '..'"}};
    }
    
    std::ofstream file(path);
    if (!file.is_open()) {
        return {{"success", false}, {"error", "Cannot write to file: " + path}};
    }
    
    file << content;
    return {{"success", true}, {"message", "File written successfully"}, {"path", path}};
}

nlohmann::json MCPManager::listDir(const nlohmann::json& args) {
    if (!args.contains("path")) {
        return {{"success", false}, {"error", "Missing 'path' argument"}};
    }
    
    std::string path = args["path"];
    
    // 安全检查
    if (path.find("..") != std::string::npos) {
        return {{"success", false}, {"error", "Invalid path: contains '..'"}};
    }
    
    std::string cmd = "ls -la \"" + path + "\" 2>&1";
    std::array<char, 512> buffer;
    std::string result;
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return {{"success", false}, {"error", "Failed to execute ls"}};
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    
    pclose(pipe);
    
    return {{"success", true}, {"listing", result}};
}

nlohmann::json MCPManager::runCommand(const nlohmann::json& args) {
    if (!args.contains("command")) {
        return {{"success", false}, {"error", "Missing 'command' argument"}};
    }
    
    std::string cmd = args["command"];
    
    // 危险命令拦截
    std::vector<std::string> dangerous = {"rm -rf", "sudo", "chmod 777", "dd if=", "> /dev/", 
                                          "mkfs", "fdisk", "wget.*\\|\\|.*curl.*\\|\\|", ":(){:|:&};:"};
    
    for (const auto& d : dangerous) {
        if (cmd.find(d) != std::string::npos) {
            return {{"success", false}, {"error", "Dangerous command blocked: " + d}};
        }
    }
    
    // 只允许安全子集
    std::vector<std::string> allowed_prefixes = {"ls ", "cat ", "head ", "tail ", "grep ", "find ", 
                                                  "pwd", "whoami", "date", "echo ", "wc "};
    
    bool allowed = false;
    for (const auto& prefix : allowed_prefixes) {
        if (cmd.find(prefix) == 0 || cmd == prefix.substr(0, prefix.length()-1)) {
            allowed = true;
            break;
        }
    }
    
    if (!allowed) {
        return {{"success", false}, {"error", "Command not in allowed safe subset"}};
    }
    
    std::array<char, 256> buffer;
    std::string result;
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return {{"success", false}, {"error", "Failed to execute command"}};
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    
    int status = pclose(pipe);
    
    return {{"success", WEXITSTATUS(status) == 0}, {"output", result}, {"exit_code", WEXITSTATUS(status)}};
}

} // namespace opencode
