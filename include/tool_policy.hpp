#ifndef TOOL_POLICY_HPP
#define TOOL_POLICY_HPP

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace opencode {

/// Effect categories for tool classification and approval tiering.
enum class ToolEffect {
    ReadOnly,          // reads files/dirs with no side effects
    WorkspaceWrite,    // creates or modifies files inside the workspace
    NetworkAccess,     // may access the network
    OutOfWorkspace,    // may read/write outside the workspace boundary
    Destructive        // may delete files, kill processes, or other hard-to-undo ops
};

/// Describes what a tool does and how it should be approved.
struct ToolEffectProfile {
    std::vector<ToolEffect> effects;
    /// When true, approval may be granted for "this exact argv for this session".
    bool scoped_approval_allowed = false;
};

/// Information presented to the user for approval.
struct ToolApproval {
    bool required = false;
    std::string action;        // human-readable action description
    std::string detail;        // target path, command, etc.
    std::string preview;       // diff preview, argv preview, etc.
    ToolEffectProfile effect;  // effect classification for UI tier display
};

class ToolPolicy {
public:
    /// Classify a tool by name and arguments, returning the approval decision
    /// and effect profile.
    static ToolApproval assess(
        const std::string& tool_name,
        const nlohmann::json& arguments
    );

    /// Return the effect profile for a known tool.  Unknown tools are
    /// treated as having all effects (Destructive + approval required).
    static ToolEffectProfile classify(const std::string& tool_name);
};

} // namespace opencode

#endif // TOOL_POLICY_HPP
