#include <iostream>
#include <string>
#include <vector>
#include <csignal>
#include <atomic>
#include <regex>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <array>
#include <memory>

#include "config_manager.hpp"
#include "token_tracker.hpp"
#include "git_manager.hpp"
#include "mcp_manager.hpp"
#include "http_client.hpp"
#include "ui.hpp"
#include "prompt.hpp"

using namespace opencode;

std::atomic<bool> g_interrupted(false);

void signalHandler(int signum) {
    g_interrupted = true;
    std::cout << "\n\n⚠  Interrupted by user. Type 'exit' to quit.\n";
}

// 执行终端命令并返回输出
std::string executeCommand(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    // 使用 popen 执行命令，重定向 stderr 到 stdout
    std::string full_cmd = cmd + " 2>&1";
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(full_cmd.c_str(), "r"), pclose);
    if (!pipe) {
        return "Error: Failed to execute command";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

// 写入文件
bool writeFile(const std::string& path, const std::string& content) {
    try {
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }
        file << content;
        file.close();
        return true;
    } catch (...) {
        return false;
    }
}

// 读取文件内容
std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// 解析并执行工具调用，并返回可追加到对话历史的工具结果
std::string executeToolCalls(const std::string& response, const std::string& work_dir, MCPManager& mcp_mgr) {
    std::vector<std::string> start_tags = {"<｜tool_calls｜>", "<｜tool_calls>"};
    std::vector<std::string> end_tags = {"<｜/tool_calls｜>", "<｜/tool_calls>"};

    size_t start_pos = std::string::npos;
    std::string start_tag_used, end_tag_used;

    for (size_t i = 0; i < start_tags.size(); ++i) {
        size_t found = response.find(start_tags[i]);
        if (found != std::string::npos) {
            start_pos = found;
            start_tag_used = start_tags[i];
            end_tag_used = end_tags[i];
            break;
        }
    }

    if (start_pos == std::string::npos) {
        return "";
    }

    size_t end_pos = response.find(end_tag_used, start_pos);
    if (end_pos == std::string::npos) {
        return "Tool call block was not executed because the closing tag was missing.";
    }

    std::string tools_content = response.substr(start_pos + start_tag_used.length(),
                                                end_pos - start_pos - start_tag_used.length());

    const std::string tool_start = "<｜tool_call name=\"";
    const std::string tool_end = "</｜tool_call>";
    std::ostringstream tool_results;
    bool executed_any = false;

    auto resolvePath = [&](const std::string& path) -> std::string {
        if (path.empty() || path[0] == '/') {
            return path;
        }
        return work_dir + "/" + path;
    };

    size_t pos = 0;
    while ((pos = tools_content.find(tool_start, pos)) != std::string::npos) {
        size_t name_start = pos + tool_start.length();
        size_t name_end = tools_content.find('"', name_start);
        if (name_end == std::string::npos) break;

        std::string tool_name = tools_content.substr(name_start, name_end - name_start);
        if (tool_name == "terminal" || tool_name == "execute_command") {
            tool_name = "run_terminal";
        }

        size_t content_start = tools_content.find('>', name_end);
        if (content_start == std::string::npos) break;
        content_start += 1;

        size_t content_end = tools_content.find(tool_end, content_start);
        if (content_end == std::string::npos) break;

        std::string params_raw = tools_content.substr(content_start, content_end - content_start);
        std::cout << "\n🔧 Executing tool: " << tool_name << std::endl;

        nlohmann::json params_json;
        try {
            params_json = nlohmann::json::parse(params_raw);
        } catch (const std::exception& e) {
            nlohmann::json result = {{"success", false}, {"error", std::string("Invalid JSON arguments: ") + e.what()}};
            std::cout << "   ❌ " << result["error"].get<std::string>() << "\n";
            tool_results << "Tool " << tool_name << ": " << result.dump() << "\n";
            pos = content_end + tool_end.length();
            executed_any = true;
            continue;
        }

        if ((tool_name == "read_file" || tool_name == "write_file" || tool_name == "edit_file" || tool_name == "list_directory") &&
            params_json.contains("path") && params_json["path"].is_string()) {
            params_json["path"] = resolvePath(params_json["path"].get<std::string>());
        }

        nlohmann::json result = mcp_mgr.executeTool(tool_name, params_json);
        std::cout << "   " << (result.value("success", false) ? "✅" : "❌") << " " << result.dump(2) << "\n";
        tool_results << "Tool " << tool_name << ": " << result.dump() << "\n";

        pos = content_end + tool_end.length();
        executed_any = true;
    }

    return executed_any ? tool_results.str() : "No valid tool calls were found in the tool call block.";
}

int main() {
    // 设置信号处理
    std::signal(SIGINT, signalHandler);
    
    UI ui;
    ConfigManager config_mgr;
    TokenTracker token_tracker;
    GitManager git_mgr;
    MCPManager mcp_mgr;
    HttpClient http_client;
    PromptManager prompt_mgr;
    
    ui.showWelcome();
    
    // 配置向导
    int provider_choice = ui.showConfigWizard();
    
    std::string api_url, api_key, model;
    ProviderType provider;
    
    switch (provider_choice) {
        case 1:  // DeepSeek
            provider = ProviderType::DeepSeek;
            api_url = "https://api.deepseek.com/v1/chat/completions";
            model = "deepseek-chat";
            break;
        case 2:  // OpenAI
            provider = ProviderType::OpenAI;
            api_url = "https://api.openai.com/v1/chat/completions";
            model = "gpt-4o-mini";
            break;
        case 3:  // Anthropic
            provider = ProviderType::Anthropic;
            api_url = "https://api.anthropic.com/v1/messages";
            model = "claude-3-5-sonnet-20241022";
            break;
        case 4:  // LlamaCpp
            provider = ProviderType::LlamaCpp;
            api_url = "http://localhost:8080/v1/chat/completions";
            model = "llama3";
            break;
        default:
            ui.showError("Invalid provider choice");
            return 1;
    }
    
    // 获取 API Key (本地模式可选)
    if (provider != ProviderType::LlamaCpp) {
        api_key = ui.getInput("Enter your API key");
        if (api_key.empty()) {
            ui.showError("API key is required");
            return 1;
        }
    } else {
        api_key = "";  // 本地模式不需要 API key
    }
    
    // 确认模型
    std::cout << "\nUsing model: " << model << "\n";
    
    // 设置 token 追踪
    token_tracker.setModel(model);
    
    // 选择工作目录
    std::string work_dir = ui.getInput("Enter working directory (default: current)");
    if (work_dir.empty()) {
        work_dir = ".";
    }
    
    // Git 集成询问
    std::string use_git_str = ui.getInput("Enable Git integration? (y/n)");
    bool use_git = (use_git_str == "y" || use_git_str == "Y");
    
    // 检查 Git 状态
    if (use_git && git_mgr.isGitAvailable()) {
        GitStatus status = git_mgr.checkStatus(work_dir);
        if (status.is_repo) {
            std::cout << "\n📁 Git repository detected: " << status.current_branch << "\n";
            if (status.has_uncommitted_changes) {
                std::cout << "⚠️  You have " << status.modified_files.size() << " uncommitted file(s)\n";
            }
            if (status.is_behind_remote) {
                std::cout << "⚠️  Local branch is " << status.commits_behind << " commit(s) behind remote\n";
            }
        } else {
            std::cout << "\n📁 Directory is not a Git repository\n";
        }
    }
    
    // 保存配置
    auto& config = config_mgr.getConfig();
    config.provider = provider;
    config.api_url = api_url;
    config.api_key = api_key;
    config.model = model;
    config.work_dir = work_dir;
    config.use_git = use_git;
    
    ui.clearScreen();
    std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Configuration Complete!                                  ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════════╣\n";
    std::cout << "║  Provider: " << ConfigManager::providerToString(provider) << std::string(50 - ConfigManager::providerToString(provider).length(), ' ') << "║\n";
    std::cout << "║  Model:    " << model << std::string(50 - model.length(), ' ') << "║\n";
    std::cout << "║  Work Dir: " << work_dir << std::string(50 - work_dir.length(), ' ') << "║\n";
    std::cout << "║  Git:      " << (use_git ? "Enabled" : "Disabled") << std::string(50 - (use_git ? 7 : 8), ' ') << "║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n\n";
    
    // 对话循环
    std::vector<ChatMessage> messages;
    messages.push_back({"system", prompt_mgr.getSystemPrompt()});
    
    std::cout << "💬 Chat started. Type 'exit' or 'quit' to end session.\n";
    std::cout << "   Type 'stats' to view token usage.\n\n";
    
    while (!g_interrupted) {
        std::string user_input = ui.getInput("You");
        
        if (user_input == "exit" || user_input == "quit") {
            break;
        }
        
        if (user_input == "stats") {
            ui.showTokenStats(
                token_tracker.getTotalInputTokens(),
                token_tracker.getTotalOutputTokens(),
                token_tracker.getTotalCostUSD()
            );
            continue;
        }
        
        if (user_input.empty()) {
            continue;
        }
        
        // 添加用户消息
        size_t messages_size_before_turn = messages.size();
        messages.push_back({"user", user_input});
        
        // 发送请求；如果模型调用工具，则把工具结果回传并自动继续一小轮
        std::string provider_str = ConfigManager::providerToString(provider);
        constexpr int kMaxToolRounds = 5;
        bool turn_failed = false;

        for (int tool_round = 0; tool_round < kMaxToolRounds; ++tool_round) {
            ChatResponse response = http_client.sendChatRequest(
                api_url,
                api_key,
                model,
                messages,
                provider_str
            );

            if (!response.success) {
                ui.showError("API Error: " + response.error_message);
                turn_failed = true;
                break;
            }

            // 显示 AI 响应
            ui.showAIResponse(response.content);

            // 记录 token 使用
            token_tracker.recordTokens(response.input_tokens, response.output_tokens);

            // 显示本次费用
            ModelPricing pricing = TokenTracker::getModelPrice(model);
            double this_cost = (static_cast<double>(response.input_tokens) / 1000000.0) * pricing.input_price_per_1m +
                              (static_cast<double>(response.output_tokens) / 1000000.0) * pricing.output_price_per_1m;

            if (this_cost > 0.000001) {
                std::cout << "   💰 This API call: $" << std::fixed << std::setprecision(6) << this_cost << " USD\n";
            }

            messages.push_back({"assistant", response.content});

            // 解析并执行工具调用，然后把结果作为下一轮上下文反馈给模型
            std::string tool_results = executeToolCalls(response.content, work_dir, mcp_mgr);
            if (tool_results.empty()) {
                break;
            }

            messages.push_back({"user", "Tool execution results:\n" + tool_results +
                                         "Use these results to continue the task. Do not repeat successful tool calls unless necessary."});

            if (tool_round == kMaxToolRounds - 1) {
                ui.showError("Tool execution stopped after reaching the maximum automatic tool rounds.");
            }
        }

        if (turn_failed) {
            messages.resize(messages_size_before_turn);  // 回滚本轮失败的对话上下文
            continue;
        }

        // 显示累计统计
        std::cout << "   📊 Session total: "
                  << token_tracker.getTotalInputTokens() + token_tracker.getTotalOutputTokens()
                  << " tokens, $" << std::fixed << std::setprecision(6)
                  << token_tracker.getTotalCostUSD() << " USD\n\n";

    }
    
    // 最终统计
    std::cout << "\n╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Session Summary                                          ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════════╣\n";
    std::cout << "║  Total Input Tokens:  " << std::setw(10) << token_tracker.getTotalInputTokens() << "                  ║\n";
    std::cout << "║  Total Output Tokens: " << std::setw(10) << token_tracker.getTotalOutputTokens() << "                  ║\n";
    std::cout << "║  ─────────────────────────────────────────────────────    ║\n";
    std::cout << "║  Estimated Cost:      $" << std::fixed << std::setprecision(6) << std::setw(8) << token_tracker.getTotalCostUSD() << " USD             ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n";
    
    std::cout << "\n👋 Goodbye!\n";
    
    return 0;
}
