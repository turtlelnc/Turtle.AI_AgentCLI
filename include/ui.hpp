#ifndef UI_HPP
#define UI_HPP

#include <string>
#include <vector>
#include "config_manager.hpp"

namespace opencode {

class UI {
public:
    UI();
    
    // 显示欢迎界面 (UTF-8 装饰)
    void showWelcome();
    
    // 配置向导 - 支持 API Key 历史和模型选择
    int showConfigWizard(ConfigManager& config_mgr, std::string& selected_model, bool& use_previous_api);
    
    // 获取用户输入
    std::string getInput(const std::string& prompt);
    
    // 显示消息
    void showMessage(const std::string& role, const std::string& content);
    
    // 显示 AI 响应（支持 UTF-8 修复）
    void showAIResponse(const std::string& content);
    
    // 显示 token 使用和费用统计
    void showTokenStats(int64_t input_tokens, int64_t output_tokens, double cost_usd);
    
    // 显示错误
    void showError(const std::string& message);
    
    // 清除屏幕
    void clearScreen();
    
    // 显示对话历史
    void showHistory(const std::vector<std::pair<std::string, std::string>>& history);
    
    // 显示工作区选择（带历史记录）
    std::string showWorkspaceSelection(ConfigManager& config_mgr);
    
    // 显示对话选择
    int showConversationSelection(ConfigManager& config_mgr, const std::string& work_dir);
    
    // 获取对话名称
    std::string getConversationName();
    
    // 显示模型选择
    std::string showModelSelection(ProviderType provider, const std::string& current_model);
    
    // 显示 API Key 配置选项
    int showApiKeyConfig(ConfigManager& config_mgr, ProviderType provider);
    
private:
    void printBanner();
    std::string getUtf8Border();
    std::string fixUtf8Encoding(const std::string& input);
};

} // namespace opencode

#endif // UI_HPP
