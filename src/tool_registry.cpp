#include "tool_registry.hpp"
#include "sandbox_runner.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <array>
#include <chrono>
#include <future>
#include <system_error>

namespace opencode {

ToolRegistry::ToolRegistry() {
    std::error_code error;
    workspace_root_ = std::filesystem::weakly_canonical(
        std::filesystem::current_path(error), error
    );
    if (error) {
        workspace_root_ = std::filesystem::path(".");
    }
    registerBuiltinTools();
}

bool ToolRegistry::setWorkspaceRoot(const std::string& path) {
    std::error_code error;
    auto root = std::filesystem::weakly_canonical(path, error);
    if (error || !std::filesystem::is_directory(root, error) || error) {
        return false;
    }
    workspace_root_ = std::move(root);
    return true;
}

void ToolRegistry::setChangeJournal(ChangeJournal* journal) {
    change_journal_ = journal;
}

void ToolRegistry::setCancellationToken(CancellationToken* token) {
    cancellation_token_ = token;
}

std::optional<std::filesystem::path> ToolRegistry::resolveWorkspacePath(
    const std::string& path,
    bool target_may_not_exist
) const {
    if (path.empty()) {
        return std::nullopt;
    }

    std::filesystem::path requested(path);
    if (requested.is_relative()) {
        requested = workspace_root_ / requested;
    }

    std::error_code error;
    std::filesystem::path resolved;
    if (target_may_not_exist) {
        const bool target_exists = std::filesystem::exists(requested, error);
        if (error) {
            return std::nullopt;
        }
        if (target_exists) {
            resolved = std::filesystem::weakly_canonical(requested, error);
        } else {
            auto parent = std::filesystem::weakly_canonical(requested.parent_path(), error);
            if (!error) {
                resolved = (parent / requested.filename()).lexically_normal();
            }
        }
        if (error) {
            return std::nullopt;
        }
    } else {
        resolved = std::filesystem::weakly_canonical(requested, error);
        if (error) {
            return std::nullopt;
        }
    }

    auto root_it = workspace_root_.begin();
    auto resolved_it = resolved.begin();
    for (; root_it != workspace_root_.end(); ++root_it, ++resolved_it) {
        if (resolved_it == resolved.end() || *root_it != *resolved_it) {
            return std::nullopt;
        }
    }
    return resolved;
}

void ToolRegistry::registerBuiltinTools() {
    // 文件读取工具
    tools_.push_back({
        "read_file",
        "Read contents of a file (max 10KB)",
        [this](const nlohmann::json& args) { return readFile(args); }
    });
    
    // 文件写入工具
    tools_.push_back({
        "write_file",
        "Write content to a file (creates or overwrites)",
        [this](const nlohmann::json& args) { return writeFile(args); }
    });
    
    // 文件编辑工具
    tools_.push_back({
        "edit_file",
        "Edit existing file by replacing text",
        [this](const nlohmann::json& args) { return editFile(args); }
    });
    
    // 目录列表工具
    tools_.push_back({
        "list_directory",
        "List files in a directory",
        [this](const nlohmann::json& args) { return listDir(args); }
    });
    
    // 终端命令执行工具 (直接 argv 执行，无 shell)
    tools_.push_back({
        "run_terminal",
        "Execute a program directly with arguments. No shell interpretation. "
        "Provide argv as an array where the first element is the executable.",
        [this](const nlohmann::json& args) { return runTerminal(args); }
    });

    // Shell 命令执行工具 (较高审批等级)
    tools_.push_back({
        "run_shell",
        "Execute a command through /bin/sh -lc. Requires elevated approval "
        "because shell metacharacters are interpreted. Prefer run_terminal.",
        [this](const nlohmann::json& args) { return runShell(args); }
    });
}

void ToolRegistry::registerTool(const RegisteredTool& tool) {
    tools_.push_back(tool);
}

std::vector<nlohmann::json> ToolRegistry::getToolsSchema() const {
    std::vector<nlohmann::json> schema;
    
    for (const auto& tool : tools_) {
        nlohmann::json s;
        s["type"] = "function";
        s["function"]["name"] = tool.name;
        s["function"]["description"] = tool.description;
        s["function"]["parameters"]["type"] = "object";
        
        if (tool.name == "read_file") {
            s["function"]["parameters"]["properties"]["path"]["type"] = "string";
            s["function"]["parameters"]["properties"]["path"]["description"] = "File path to read";
            s["function"]["parameters"]["required"] = nlohmann::json::array({"path"});
        } else if (tool.name == "write_file") {
            s["function"]["parameters"]["properties"]["path"]["type"] = "string";
            s["function"]["parameters"]["properties"]["path"]["description"] = "File path (relative or absolute)";
            s["function"]["parameters"]["properties"]["content"]["type"] = "string";
            s["function"]["parameters"]["properties"]["content"]["description"] = "Content to write";
            s["function"]["parameters"]["required"] = nlohmann::json::array({"path", "content"});
        } else if (tool.name == "edit_file") {
            s["function"]["parameters"]["properties"]["path"]["type"] = "string";
            s["function"]["parameters"]["properties"]["path"]["description"] = "File path";
            s["function"]["parameters"]["properties"]["old_text"]["type"] = "string";
            s["function"]["parameters"]["properties"]["old_text"]["description"] = "Text to find and replace";
            s["function"]["parameters"]["properties"]["new_text"]["type"] = "string";
            s["function"]["parameters"]["properties"]["new_text"]["description"] = "Replacement text";
            s["function"]["parameters"]["required"] = nlohmann::json::array({"path", "old_text", "new_text"});
        } else if (tool.name == "list_directory") {
            s["function"]["parameters"]["properties"]["path"]["type"] = "string";
            s["function"]["parameters"]["properties"]["path"]["description"] = "Directory path (default: '.')";
            s["function"]["parameters"]["required"] = nlohmann::json::array({"path"});
        } else if (tool.name == "run_terminal") {
            s["function"]["parameters"]["properties"]["argv"] = {
                {"type", "array"},
                {"description",
                 "Program and arguments as an array. "
                 "The first element is the executable; "
                 "no shell interpretation is performed."},
                {"items", {{"type", "string"}}},
                {"minItems", 1}
            };
            s["function"]["parameters"]["properties"]["cwd"]["type"] = "string";
            s["function"]["parameters"]["properties"]["cwd"]["description"] =
                "Working directory for the command (default: workspace root)";
            s["function"]["parameters"]["properties"]["timeout_seconds"]["type"] = "integer";
            s["function"]["parameters"]["properties"]["timeout_seconds"]["minimum"] = 1;
            s["function"]["parameters"]["properties"]["timeout_seconds"]["maximum"] = 120;
            s["function"]["parameters"]["required"] = nlohmann::json::array({"argv"});
        } else if (tool.name == "run_shell") {
            s["function"]["parameters"]["properties"]["command"]["type"] = "string";
            s["function"]["parameters"]["properties"]["command"]["description"] =
                "Shell command to execute via /bin/sh -lc. "
                "This requires elevated approval.";
            s["function"]["parameters"]["properties"]["timeout_seconds"]["type"] = "integer";
            s["function"]["parameters"]["properties"]["timeout_seconds"]["minimum"] = 1;
            s["function"]["parameters"]["properties"]["timeout_seconds"]["maximum"] = 60;
            s["function"]["parameters"]["required"] = nlohmann::json::array({"command"});
        } else if (!tool.parameters.is_null()) {
            s["function"]["parameters"] = tool.parameters;
        }
        
        schema.push_back(s);
    }
    
    return schema;
}

nlohmann::json ToolRegistry::executeTool(const std::string& name, const nlohmann::json& args) {
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

bool ToolRegistry::hasTool(const std::string& name) const {
    for (const auto& tool : tools_) {
        if (tool.name == name) return true;
    }
    return false;
}

nlohmann::json ToolRegistry::readFile(const nlohmann::json& args) {
    if (!args.contains("path")) {
        return {{"success", false}, {"error", "Missing 'path' argument"}};
    }
    
    auto path = resolveWorkspacePath(args["path"].get<std::string>());
    if (!path) {
        return {{"success", false}, {"error", "Path is outside the workspace or cannot be resolved"}};
    }
    
    std::ifstream file(*path);
    if (!file.is_open()) {
        return {{"success", false}, {"error", "Cannot open file: " + path->string()}};
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    const auto original_size = static_cast<int64_t>(content.size());

    // 限制文件大小 (10KB)
    if (content.size() > 10240) {
        content = content.substr(0, 10240) + "\n... [truncated]";
    }

    return {{"success", true}, {"content", content}, {"size", original_size}};
}

nlohmann::json ToolRegistry::writeFile(const nlohmann::json& args) {
    if (!args.contains("path") || !args.contains("content")) {
        return {{"success", false}, {"error", "Missing 'path' or 'content' argument"}};
    }
    
    auto path = resolveWorkspacePath(args["path"].get<std::string>(), true);
    std::string content = args["content"];
    
    if (!path) {
        return {{"success", false}, {"error", "Path is outside the workspace or cannot be resolved"}};
    }
    
    ChangeJournal::Snapshot before;
    if (change_journal_) before = change_journal_->capture(*path);
    std::filesystem::create_directories(path->parent_path());
    std::ofstream file(*path);
    if (!file.is_open()) {
        return {{"success", false}, {"error", "Cannot write to file: " + path->string()}};
    }
    
    file << content;
    file.close();
    nlohmann::json result = {
        {"success", true}, {"message", "File written successfully"}, {"path", path->string()}
    };
    if (change_journal_) {
        result["change_id"] =
            change_journal_->record("write_file", *path, std::move(before));
    }
    return result;
}

nlohmann::json ToolRegistry::editFile(const nlohmann::json& args) {
    if (!args.contains("path") || !args.contains("old_text") || !args.contains("new_text")) {
        return {{"success", false}, {"error", "Missing 'path', 'old_text', or 'new_text' argument"}};
    }
    
    auto path = resolveWorkspacePath(args["path"].get<std::string>());
    std::string old_text = args["old_text"];
    std::string new_text = args["new_text"];
    
    if (!path) {
        return {{"success", false}, {"error", "Path is outside the workspace or cannot be resolved"}};
    }
    
    ChangeJournal::Snapshot before;
    if (change_journal_) before = change_journal_->capture(*path);
    // 读取文件
    std::ifstream file(*path);
    if (!file.is_open()) {
        return {{"success", false}, {"error", "Cannot open file: " + path->string()}};
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();
    
    // 查找并替换文本
    size_t pos = content.find(old_text);
    if (pos == std::string::npos) {
        return {{"success", false}, {"error", "Text to replace not found in file"}};
    }
    
    content.replace(pos, old_text.length(), new_text);
    
    // 写回文件
    std::ofstream out_file(*path);
    if (!out_file.is_open()) {
        return {{"success", false}, {"error", "Cannot write to file: " + path->string()}};
    }
    
    out_file << content;
    out_file.close();
    nlohmann::json result = {
        {"success", true}, {"message", "File edited successfully"}, {"path", path->string()}
    };
    if (change_journal_) {
        result["change_id"] =
            change_journal_->record("edit_file", *path, std::move(before));
    }
    return result;
}

nlohmann::json ToolRegistry::listDir(const nlohmann::json& args) {
    if (!args.contains("path")) {
        return {{"success", false}, {"error", "Missing 'path' argument"}};
    }
    
    auto path = resolveWorkspacePath(args["path"].get<std::string>());
    if (!path) {
        return {{"success", false}, {"error", "Path is outside the workspace or cannot be resolved"}};
    }
    
    std::error_code error;
    if (!std::filesystem::is_directory(*path, error) || error) {
        return {{"success", false}, {"error", "Not a readable directory: " + path->string()}};
    }

    nlohmann::json entries = nlohmann::json::array();
    for (std::filesystem::directory_iterator it(*path, error), end;
         !error && it != end; it.increment(error)) {
        const auto& entry = *it;
        entries.push_back({
            {"name", entry.path().filename().string()},
            {"type", entry.is_directory(error) ? "directory" :
                     entry.is_regular_file(error) ? "file" : "other"}
        });
        if (entries.size() >= 1000) {
            break;
        }
    }
    if (error) {
        return {{"success", false}, {"error", "Failed to list directory: " + error.message()}};
    }

    return {{"success", true}, {"entries", entries}, {"truncated", entries.size() >= 1000}};
}

nlohmann::json ToolRegistry::runTerminal(const nlohmann::json& args) {
    if (!args.contains("argv") || !args["argv"].is_array() || args["argv"].empty()) {
        return {{"success", false}, {"error", "Missing or invalid 'argv' argument — must be a non-empty string array"}};
    }

    std::vector<std::string> argv;
    for (const auto& elem : args["argv"]) {
        if (!elem.is_string()) {
            return {{"success", false}, {"error", "Each argv element must be a string"}};
        }
        argv.push_back(elem.get<std::string>());
    }

    int timeout_seconds = args.value("timeout_seconds", 30);
    if (timeout_seconds < 1 || timeout_seconds > 120) {
        return {{"success", false}, {"error", "timeout_seconds must be between 1 and 120"}};
    }

    // Supplementary dangerous-pattern check — not the primary security boundary.
    // The primary boundary is: direct exec (no shell), sandbox, and workspace
    // path containment.
    const std::vector<std::string> dangerous = {
        "rm -rf", "sudo", "chmod 777", "dd if=", "mkfs", "fdisk"
    };
    for (const auto& arg : argv) {
        for (const auto& d : dangerous) {
            if (arg.find(d) != std::string::npos) {
                return {{"success", false}, {"error", "Dangerous argument blocked (supplementary check): " + d}};
            }
        }
    }

    if (!SandboxRunner::isAvailable()) {
        return {
            {"success", false},
            {"error", "Command execution refused because no native sandbox backend is available"}
        };
    }

    SandboxResult sandbox_result = SandboxRunner::run(
        argv, workspace_root_, timeout_seconds, 102400, cancellation_token_
    );
    if (!sandbox_result.launched) {
        return {{"success", false}, {"error", sandbox_result.error}};
    }

    return {
        {"success", sandbox_result.exit_code == 0},
        {"output", sandbox_result.output},
        {"exit_code", sandbox_result.exit_code},
        {"timed_out", sandbox_result.timed_out},
        {"cancelled", sandbox_result.cancelled},
        {"sandbox", "macos-seatbelt"}
    };
}

nlohmann::json ToolRegistry::runShell(const nlohmann::json& args) {
    if (!args.contains("command") || !args["command"].is_string()) {
        return {{"success", false}, {"error", "Missing 'command' argument"}};
    }

    std::string cmd = args["command"].get<std::string>();
    int timeout_seconds = args.value("timeout_seconds", 30);
    if (timeout_seconds < 1 || timeout_seconds > 60) {
        return {{"success", false}, {"error", "timeout_seconds must be between 1 and 60"}};
    }

    // Shell-based execution: supplementary dangerous-command detection.
    const std::vector<std::string> dangerous = {
        "rm -rf", "sudo", "chmod 777", "dd if=", "> /dev/",
        "mkfs", "fdisk", ":(){:|:&};:"
    };
    for (const auto& d : dangerous) {
        if (cmd.find(d) != std::string::npos) {
            return {{"success", false}, {"error", "Dangerous shell command blocked: " + d}};
        }
    }

    if (!SandboxRunner::isAvailable()) {
        return {
            {"success", false},
            {"error", "Shell execution refused because no native sandbox backend is available"}
        };
    }

    SandboxResult sandbox_result = SandboxRunner::runShell(
        cmd, workspace_root_, timeout_seconds, 102400, cancellation_token_
    );
    if (!sandbox_result.launched) {
        return {{"success", false}, {"error", sandbox_result.error}};
    }

    return {
        {"success", sandbox_result.exit_code == 0},
        {"output", sandbox_result.output},
        {"exit_code", sandbox_result.exit_code},
        {"timed_out", sandbox_result.timed_out},
        {"cancelled", sandbox_result.cancelled},
        {"sandbox", "macos-seatbelt"}
    };
}

} // namespace opencode
