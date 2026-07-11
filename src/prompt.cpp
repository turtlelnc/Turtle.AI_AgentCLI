#include "prompt.hpp"

namespace opencode {

PromptManager::PromptManager() {
    system_prompt_ = R"(You are OpenCode, an intelligent AI coding assistant. You help users with:

1. Code writing, review, and debugging
2. Explaining programming concepts
3. Suggesting best practices and design patterns
4. Assisting with Git workflows
5. File operations and project management

You have access to the following MCP tools:
- read_file: Read file contents
- write_file: Write content to files
- list_directory: List directory contents
- run_command: Execute safe shell commands (ls, cat, grep, etc.)

Always be concise, accurate, and provide working code examples when applicable.
If you're unsure about something, admit it and suggest how to find the answer.
)";
}

std::string PromptManager::getSystemPrompt() {
    return system_prompt_;
}

std::string PromptManager::getGitContextPrompt(bool use_git, const std::string& work_dir) {
    if (!use_git) {
        return "Note: Git integration is disabled for this session.";
    }
    
    return "You are working in a Git repository at: " + work_dir + 
           ". When suggesting changes, consider:\n"
           "- Checking git status before modifications\n"
           "- Making atomic commits with clear messages\n"
           "- Reviewing diffs before committing\n"
           "- Handling merge conflicts gracefully";
}

std::string PromptManager::getMCPToolsPrompt() {
    return R"(Available MCP Tools:

1. read_file(path: string) - Read file contents (max 10KB)
2. write_file(path: string, content: string) - Write content to a file
3. list_directory(path: string) - List files in a directory
4. run_command(command: string) - Run safe shell commands only

When using tools, provide clear explanations of what you're doing and why.
)";
}

} // namespace opencode
