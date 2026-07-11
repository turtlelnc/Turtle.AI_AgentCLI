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

// 解析并执行工具调用
void executeToolCalls(const std::string& response, const std::string& work_dir) {
    // 匹配 DeepSeek/Claude 风格的工具调用：<｜tool_calls｜>...<｜/tool_calls｜>
    // 支持两种格式：带竖线和不带竖线
    std::vector<std::string> start_tags = {"<｜tool_calls｜>", "<｜tool_calls>"};
    std::vector<std::string> end_tags = {"<｜/tool_calls｜>", "<｜/tool_calls>"};
    
    size_t start_pos = std::string::npos;
    std::string start_tag_used, end_tag_used;
    
    for (size_t i = 0; i < start_tags.size(); ++i) {
        size_t pos = response.find(start_tags[i]);
        if (pos != std::string::npos) {
            start_pos = pos;
            start_tag_used = start_tags[i];
            end_tag_used = end_tags[i];
            break;
        }
    }
    
    if (start_pos == std::string::npos) {
        return;  // 没有工具调用
    }
    
    size_t end_pos = response.find(end_tag_used, start_pos);
    if (end_pos == std::string::npos) {
        return;  // 格式不完整
    }
    
    std::string tools_content = response.substr(start_pos + start_tag_used.length(), 
                                                 end_pos - start_pos - start_tag_used.length());
    
    // 提取每个工具调用 <｜tool_call name="...">...</｜tool_call>
    std::vector<std::string> tool_start_tags = {"<｜tool_call name=\"", "<｜tool_call name=\""};
    std::string tool_end = "</｜tool_call>";
    
    size_t pos = 0;
    while ((pos = tools_content.find(tool_start_tags[0], pos)) != std::string::npos) {
        size_t name_start = pos + tool_start_tags[0].length();
        size_t name_end = tools_content.find("\"", name_start);
        if (name_end == std::string::npos) break;
        
        std::string tool_name = tools_content.substr(name_start, name_end - name_start);
        
        size_t content_start = tools_content.find(">", name_end);
        if (content_start == std::string::npos) break;
        content_start += 1;
        
        size_t content_end = tools_content.find(tool_end, content_start);
        if (content_end == std::string::npos) break;
        
        std::string params_raw = tools_content.substr(content_start, content_end - content_start);
        
        std::cout << "\n🔧 Executing tool: " << tool_name << std::endl;
        
        // 尝试解析为 JSON 首先
        nlohmann::json params_json;
        bool is_json = false;
        try {
            params_json = nlohmann::json::parse(params_raw);
            is_json = true;
        } catch (...) {
            // 不是有效 JSON，使用 XML 解析回退
        }
        
        auto extractParamJson = [&](const std::string& param_name) -> std::string {
            if (!is_json || !params_json.contains(param_name)) return "";
            return params_json[param_name].get<std::string>();
        };
        
        auto extractParamXml = [](const std::string& xml, const std::string& param_name) -> std::string {
            std::string open_tag = "<" + param_name + ">";
            std::string close_tag = "</" + param_name + ">";
            
            size_t start = xml.find(open_tag);
            if (start == std::string::npos) return "";
            
            start += open_tag.length();
            size_t end = xml.find(close_tag, start);
            if (end == std::string::npos) return "";
            
            return xml.substr(start, end - start);
        };
        
        auto extractParam = [&](const std::string& param_name) -> std::string {
            return is_json ? extractParamJson(param_name) : extractParamXml(params_raw, param_name);
        };
        
        if (tool_name == "write_file") {
            std::string path = extractParam("path");
            std::string content = extractParam("content");
            
            if (path.empty()) {
                std::cout << "   ❌ Missing 'path' parameter\n";
                pos = content_end + tool_end.length();
                continue;
            }
            
            // 如果是相对路径，拼接工作目录
            if (path[0] != '/') {
                path = work_dir + "/" + path;
            }
            
            std::cout << "   📝 Writing to: " << path << std::endl;
            if (writeFile(path, content)) {
                std::cout << "   ✅ File written successfully\n";
            } else {
                std::cout << "   ❌ Failed to write file\n";
            }
            
        } else if (tool_name == "read_file") {
            std::string path = extractParam("path");
            if (path.empty()) {
                std::cout << "   ❌ Missing 'path' parameter\n";
                pos = content_end + tool_end.length();
                continue;
            }
            if (path[0] != '/') {
                path = work_dir + "/" + path;
            }
            
            std::cout << "   📖 Reading: " << path << std::endl;
            std::string content = readFile(path);
            if (!content.empty()) {
                std::cout << "   ───────────────────────────────\n";
                std::cout << content;
                if (content.back() != '\n') std::cout << "\n";
                std::cout << "   ───────────────────────────────\n";
            } else {
                std::cout << "   ❌ Failed to read file or file not found\n";
            }
            
        } else if (tool_name == "edit_file") {
            std::string path = extractParam("path");
            std::string old_text = extractParam("old_text");
            std::string new_text = extractParam("new_text");
            
            if (path.empty()) {
                std::cout << "   ❌ Missing 'path' parameter\n";
                pos = content_end + tool_end.length();
                continue;
            }
            if (path[0] != '/') {
                path = work_dir + "/" + path;
            }
            
            std::cout << "   ✏️  Editing: " << path << std::endl;
            std::string content = readFile(path);
            if (!content.empty() && !old_text.empty()) {
                size_t pos_found = content.find(old_text);
                if (pos_found != std::string::npos) {
                    content.replace(pos_found, old_text.length(), new_text);
                    if (writeFile(path, content)) {
                        std::cout << "   ✅ File edited successfully\n";
                    } else {
                        std::cout << "   ❌ Failed to save edited file\n";
                    }
                } else {
                    std::cout << "   ❌ Old text not found in file\n";
                }
            } else {
                std::cout << "   ❌ Failed to read file or empty old_text\n";
            }
            
        } else if (tool_name == "run_terminal" || tool_name == "terminal" || tool_name == "execute_command") {
            std::string command = extractParam("command");
            
            if (!command.empty()) {
                std::cout << "   💻 Executing: " << command << std::endl;
                std::string output = executeCommand(command);
                if (!output.empty()) {
                    std::cout << "   ───────────────────────────────\n";
                    std::cout << output;
                    if (output.back() != '\n') std::cout << "\n";
                    std::cout << "   ───────────────────────────────\n";
                } else {
                    std::cout << "   (No output)\n";
                }
            }
            
        } else if (tool_name == "list_directory") {
            std::string path = extractParam("path");
            if (path.empty()) path = work_dir;
            if (path[0] != '/') {
                path = work_dir + "/" + path;
            }
            
            std::cout << "   📁 Listing: " << path << std::endl;
            std::string cmd = "ls -la \"" + path + "\"";
            std::string output = executeCommand(cmd);
            if (!output.empty()) {
                std::cout << "   ───────────────────────────────\n";
                std::cout << output;
                if (output.back() != '\n') std::cout << "\n";
                std::cout << "   ───────────────────────────────\n";
            }
        } else {
            std::cout << "   ⚠️  Unknown tool: " << tool_name << "\n";
        }
        
        pos = content_end + tool_end.length();
    }
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
        messages.push_back({"user", user_input});
        
        // 发送请求
        std::string provider_str = ConfigManager::providerToString(provider);
        ChatResponse response = http_client.sendChatRequest(
            api_url,
            api_key,
            model,
            messages,
            provider_str
        );
        
        if (!response.success) {
            ui.showError("API Error: " + response.error_message);
            messages.pop_back();  // 移除失败的用户消息
            continue;
        }
        
        // 显示 AI 响应
        ui.showAIResponse(response.content);
        
        // 解析并执行工具调用 (在添加到历史之前)
        executeToolCalls(response.content, work_dir);
        
        // 记录 token 使用
        token_tracker.recordTokens(response.input_tokens, response.output_tokens);
        
        // 显示本次费用
        ModelPricing pricing = TokenTracker::getModelPrice(model);
        double this_cost = (static_cast<double>(response.input_tokens) / 1000000.0) * pricing.input_price_per_1m +
                          (static_cast<double>(response.output_tokens) / 1000000.0) * pricing.output_price_per_1m;
        
        if (this_cost > 0.000001) {
            std::cout << "   💰 This turn: $" << std::fixed << std::setprecision(6) << this_cost << " USD\n";
        }
        
        // 添加 AI 响应到历史
        messages.push_back({"assistant", response.content});
        
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
