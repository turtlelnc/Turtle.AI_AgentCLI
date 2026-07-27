#ifndef UI_HPP
#define UI_HPP

#include <string>
#include <vector>
#include <atomic>
#include <filesystem>
#include <thread>
#include "config_manager.hpp"

namespace opencode {

class UI {
public:
    UI();
    ~UI();
    
    // 显示欢迎界面 (UTF-8 装饰)
    void showWelcome();
    
    // 配置向导 - 支持 API Key 历史和模型选择
    int showConfigWizard(ConfigManager& config_mgr, std::string& selected_model, bool& use_previous_api);
    
    // 获取用户输入
    std::string getInput(const std::string& prompt);

    // 获取不回显的敏感输入
    std::string getSecretInput(const std::string& prompt);
    
    // 显示消息
    void showMessage(const std::string& role, const std::string& content);
    
    // 显示 AI 响应（支持 UTF-8 修复）
    void showAIResponse(const std::string& content);

    void beginStreamingResponse();
    void appendStreamingChunk(const std::string& content);
    void endStreamingResponse(double elapsed_seconds);

    void startThinking();
    void stopThinking();
    
    // 显示 token 使用和费用统计
    void showTokenStats(int64_t input_tokens, int64_t output_tokens, double cost_usd);
    
    // 显示错误
    void showError(const std::string& message);

    // 审批会产生副作用的工具调用
    bool confirmToolCall(
        const std::string& action,
        const std::string& detail,
        const std::string& preview = ""
    );

    void showToolCall(const std::string& name, const std::string& detail);
    void showToolResult(const std::string& name, bool success, const std::string& summary);
    void showCollapsibleDiff(
        const std::string& diff,
        bool allow_interaction = true
    );

    void showHelp();
    void showConfigurationSummary(const Config& config);
    void showAppearanceSettings();
    bool setTheme(const std::string& theme);
    bool setAnimations(bool enabled);
    bool setMouseLinks(bool enabled);
    void enableTaskPanel();
    void setTaskPanel(
        const std::string& main_task,
        const std::vector<std::string>& subtasks,
        const std::string& usage
    );
    std::size_t selectedTaskIndex() const { return selected_task_index_; }
    
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
    bool terminal_interactive_;
    bool colors_enabled_;
    bool animations_enabled_;
    bool mouse_links_enabled_;
    std::atomic<bool> thinking_;
    std::thread thinking_thread_;
    std::string streaming_content_;
    std::string streaming_lookahead_;
    std::vector<std::string> streaming_table_lines_;
    bool streaming_in_table_;
    bool streaming_has_lookahead_;
    bool streaming_direct_line_;
    std::string theme_name_;
    std::string accent_color_;
    std::string success_color_;
    std::string warning_color_;
    std::string error_color_;
    std::filesystem::path workspace_root_;
    bool task_panel_enabled_ = false;
    std::string main_task_;
    std::vector<std::string> panel_subtasks_;
    std::string panel_usage_;
    std::size_t selected_task_index_ = 0;

    void printBanner();
    std::string getUtf8Border();
    std::string fixUtf8Encoding(const std::string& input);
    std::string color(const std::string& text, const std::string& code) const;
    std::string fileLink(const std::string& path) const;
    void renderMarkdown(const std::string& content) const;
    void processStreamingLine(const std::string& line);
    void flushStreamingTable();
    std::size_t terminalColumns() const;
    std::size_t terminalRows() const;
    void drawTaskPanel();
    std::string getTaskPanelInput(const std::string& prompt);
};

} // namespace opencode

#endif // UI_HPP
