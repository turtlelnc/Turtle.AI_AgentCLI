#ifndef UI_HPP
#define UI_HPP

#include <string>
#include <vector>

namespace opencode {

class UI {
public:
    UI();
    
    // 显示欢迎界面 (UTF-8 装饰)
    void showWelcome();
    
    // 显示配置向导
    int showConfigWizard();
    
    // 获取用户输入
    std::string getInput(const std::string& prompt);
    
    // 显示消息
    void showMessage(const std::string& role, const std::string& content);
    
    // 显示 AI 响应
    void showAIResponse(const std::string& content);
    
    // 显示 token 使用和费用统计
    void showTokenStats(int64_t input_tokens, int64_t output_tokens, double cost_usd);
    
    // 显示错误
    void showError(const std::string& message);
    
    // 清除屏幕
    void clearScreen();
    
    // 显示对话历史
    void showHistory(const std::vector<std::pair<std::string, std::string>>& history);
    
private:
    void printBanner();
    std::string getUtf8Border();
};

} // namespace opencode

#endif // UI_HPP
