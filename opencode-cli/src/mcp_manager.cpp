#include "mcp_manager.hpp"
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <sys/stat.h>

namespace opencode {

McpManager::McpManager() {
    register_builtin_tools();
}

void McpManager::register_tool(const McpTool& tool) {
    tools_.push_back(tool);
}

std::vector<std::string> McpManager::list_tools() const {
    std::vector<std::string> names;
    for (const auto& tool : tools_) {
        names.push_back(tool.name);
    }
    return names;
}

std::string McpManager::execute_tool(const std::string& name, const nlohmann::json& params) {
    for (const auto& tool : tools_) {
        if (tool.name == name) {
            try {
                return tool.handler(params);
            } catch (const std::exception& e) {
                return std::string("Error: ") + e.what();
            }
        }
    }
    return "Error: Tool '" + name + "' not found";
}

void McpManager::register_builtin_tools() {
    // Read file tool
    McpTool read_file;
    read_file.name = "read_file";
    read_file.description = "Read contents of a file";
    read_file.schema = R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})";
    read_file.handler = [this](const nlohmann::json& p) { return handle_read_file(p); };
    register_tool(read_file);
    
    // List directory tool
    McpTool list_dir;
    list_dir.name = "list_dir";
    list_dir.description = "List contents of a directory";
    list_dir.schema = R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})";
    list_dir.handler = [this](const nlohmann::json& p) { return handle_list_dir(p); };
    register_tool(list_dir);
    
    // Search files tool
    McpTool search_files;
    search_files.name = "search_files";
    search_files.description = "Search for files by pattern";
    search_files.schema = R"({"type":"object","properties":{"pattern":{"type":"string"},"path":{"type":"string"}},"required":["pattern"]})";
    search_files.handler = [this](const nlohmann::json& p) { return handle_search_files(p); };
    register_tool(search_files);
}

std::string McpManager::handle_read_file(const nlohmann::json& params) {
    if (!params.contains("path")) {
        return "Error: 'path' parameter required";
    }
    
    std::string path = params["path"].get<std::string>();
    std::ifstream file(path);
    if (!file.is_open()) {
        return "Error: Cannot open file: " + path;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string McpManager::handle_list_dir(const nlohmann::json& params) {
    if (!params.contains("path")) {
        return "Error: 'path' parameter required";
    }
    
    std::string path = params["path"].get<std::string>();
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return "Error: Cannot open directory: " + path;
    }
    
    std::stringstream result;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name != "." && name != "..") {
            std::string full_path = path + "/" + name;
            struct stat st;
            if (stat(full_path.c_str(), &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    result << "[DIR]  " << name << "\n";
                } else {
                    result << "[FILE] " << name << "\n";
                }
            }
        }
    }
    closedir(dir);
    
    return result.str();
}

std::string McpManager::handle_search_files(const nlohmann::json& params) {
    if (!params.contains("pattern")) {
        return "Error: 'pattern' parameter required";
    }
    
    std::string pattern = params["pattern"].get<std::string>();
    std::string search_path = "/";
    if (params.contains("path")) {
        search_path = params["path"].get<std::string>();
    }
    
    std::string cmd = "find \"" + search_path + "\" -name \"*" + pattern + "*\" 2>/dev/null";
    std::array<char, 256> buffer;
    std::string result;
    
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        return "Error: Failed to execute search";
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    
    return result;
}

} // namespace opencode
