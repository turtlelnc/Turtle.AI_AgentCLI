#include "tool_policy.hpp"

#include <sstream>

namespace opencode {

namespace {

std::string stringArgument(
    const nlohmann::json& arguments,
    const std::string& name,
    const std::string& fallback
) {
    if (!arguments.contains(name) || !arguments[name].is_string()) {
        return fallback;
    }
    return arguments[name].get<std::string>();
}

std::string prefixedPreview(
    const std::string& content,
    const std::string& prefix,
    std::size_t max_lines = 16
) {
    std::istringstream input(content);
    std::ostringstream output;
    std::string line;
    std::size_t count = 0;
    while (count < max_lines && std::getline(input, line)) {
        output << prefix << line << '\n';
        ++count;
    }
    if (input.good()) {
        output << "... preview truncated\n";
    }
    return output.str();
}

/// Build a human-readable argv display:  program arg1 arg2 ...
std::string argvPreview(const nlohmann::json& arguments) {
    if (!arguments.contains("argv") || !arguments["argv"].is_array()) {
        if (arguments.contains("command") && arguments["command"].is_string()) {
            return "$ " + arguments["command"].get<std::string>();
        }
        return "<missing argv>";
    }
    std::ostringstream out;
    const auto& args = arguments["argv"];
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) out << ' ';
        const std::string& arg = args[i].get_ref<const std::string&>();
        // Quote arguments that contain spaces
        if (arg.find(' ') != std::string::npos ||
            arg.find('\t') != std::string::npos) {
            out << '"' << arg << '"';
        } else {
            out << arg;
        }
        if (out.str().size() > 200) {
            out << " ...";
            break;
        }
    }
    return out.str();
}

} // namespace

ToolEffectProfile ToolPolicy::classify(const std::string& tool_name) {
    if (tool_name == "read_file" || tool_name == "list_directory" ||
        tool_name == "list_skills" || tool_name == "load_skill" ||
        tool_name == "goal_status" || tool_name == "update_subtask" ||
        tool_name == "create_subtask") {
        return {{ToolEffect::ReadOnly}, false};
    }
    if (tool_name == "write_file") {
        return {{ToolEffect::WorkspaceWrite}, false};
    }
    if (tool_name == "edit_file") {
        return {{ToolEffect::WorkspaceWrite}, false};
    }
    if (tool_name == "manage_skill") {
        return {{ToolEffect::WorkspaceWrite, ToolEffect::Destructive}, false};
    }
    if (tool_name == "run_terminal") {
        // Direct argv execution — sandboxed, no shell injection surface.
        return {
            {ToolEffect::WorkspaceWrite, ToolEffect::NetworkAccess,
             ToolEffect::Destructive},
            true   // scoped approval: allow same argv prefix for session
        };
    }
    if (tool_name == "run_shell") {
        // Shell-based execution — higher risk, elevated approval.
        return {
            {ToolEffect::WorkspaceWrite, ToolEffect::NetworkAccess,
             ToolEffect::Destructive, ToolEffect::OutOfWorkspace},
            false  // no scoped approval for shell commands
        };
    }

    // Unknown / custom tools: assume worst case.
    return {
        {ToolEffect::ReadOnly, ToolEffect::WorkspaceWrite,
         ToolEffect::NetworkAccess, ToolEffect::OutOfWorkspace,
         ToolEffect::Destructive},
        false
    };
}

ToolApproval ToolPolicy::assess(
    const std::string& tool_name,
    const nlohmann::json& arguments
) {
    ToolEffectProfile profile = classify(tool_name);
    const bool requires_approval =
        profile.effects.size() != 1 ||
        profile.effects[0] != ToolEffect::ReadOnly;

    if (!requires_approval) {
        return {false, "", "", "", std::move(profile)};
    }

    if (tool_name == "write_file") {
        const std::string path =
            stringArgument(arguments, "path", "<missing path>");
        return {
            true,
            "Write or overwrite a file",
            path,
            "--- /dev/null\n+++ " + path + "\n" +
                prefixedPreview(stringArgument(arguments, "content", ""), "+ "),
            std::move(profile)
        };
    }
    if (tool_name == "edit_file") {
        const std::string path =
            stringArgument(arguments, "path", "<missing path>");
        return {
            true,
            "Edit a file",
            path,
            "--- " + path + "\n+++ " + path + "\n" +
                prefixedPreview(stringArgument(arguments, "old_text", ""), "- ") +
                prefixedPreview(stringArgument(arguments, "new_text", ""), "+ "),
            std::move(profile)
        };
    }
    if (tool_name == "manage_skill") {
        const std::string action =
            stringArgument(arguments, "action", "<missing action>");
        const std::string name =
            stringArgument(arguments, "name", "<missing name>");
        return {
            true,
            action == "delete" ? "Delete a workspace skill"
                               : "Create or replace a workspace skill",
            "$" + name,
            action == "delete"
                ? "- .turtle/skills/" + name
                : prefixedPreview(
                    stringArgument(arguments, "content", ""), "+ "
                ),
            std::move(profile)
        };
    }
    if (tool_name == "run_terminal") {
        return {
            true,
            "Run a sandboxed command (direct execution, no shell)",
            argvPreview(arguments),
            "[argv] " + argvPreview(arguments),
            std::move(profile)
        };
    }
    if (tool_name == "run_shell" ||
        tool_name == "terminal" ||
        tool_name == "execute_command") {
        const std::string cmd =
            stringArgument(arguments, "command", "<missing command>");
        return {
            true,
            "Run a shell command (elevated risk — shell interpretation applies)",
            cmd,
            "$ " + cmd,
            profile  // run_shell has its own profile; legacy names get elevated
        };
    }

    // Unknown and custom tools require approval with full effects.
    return {
        true,
        "Run tool",
        tool_name,
        arguments.dump(2),
        std::move(profile)
    };
}

} // namespace opencode
