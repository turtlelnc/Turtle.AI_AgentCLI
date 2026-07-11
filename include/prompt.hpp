#ifndef PROMPT_HPP
#define PROMPT_HPP

#include <string>

namespace opencode {

class PromptManager {
public:
    PromptManager();
    
    // 获取系统提示词
    std::string getSystemPrompt();
    
    // 获取 Git 相关提示词
    std::string getGitContextPrompt(bool use_git, const std::string& work_dir);
    
    // 获取 MCP 工具描述
    std::string getMCPToolsPrompt();
    
private:
    std::string system_prompt_;
};

} // namespace opencode

#endif // PROMPT_HPP
