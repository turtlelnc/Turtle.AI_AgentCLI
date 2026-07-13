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
#include <cctype>

#include "config_manager.hpp"
#include "token_tracker.hpp"
#include "git_manager.hpp"
#include "mcp_manager.hpp"
#include "http_client.hpp"
#include "ui.hpp"
#include "prompt.hpp"

using namespace opencode;

std::atomic<bool> g_interrupted(false);

void signalHandler(int /* signum */) {
    g_interrupted = true;
    std::cout << "\n⚠  Interrupted by user. Type 'exit' to quit.\n";
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

namespace {

struct ParsedToolCall {
    std::string name;
    std::string body;
};

std::string trimWhitespace(const std::string& input) {
    size_t first = 0;
    while (first < input.size() && std::isspace(static_cast<unsigned char>(input[first]))) {
        ++first;
    }

    size_t last = input.size();
    while (last > first && std::isspace(static_cast<unsigned char>(input[last - 1]))) {
        --last;
    }

    return input.substr(first, last - first);
}

std::string stripMarkdownFence(const std::string& input) {
    std::string trimmed = trimWhitespace(input);
    if (trimmed.rfind("```", 0) != 0) {
        return input;
    }

    size_t first_line_end = trimmed.find('\n');
    if (first_line_end == std::string::npos) {
        return input;
    }

    size_t closing_fence = trimmed.rfind("```");
    if (closing_fence == 0 || closing_fence == std::string::npos) {
        return input;
    }

    std::string body = trimmed.substr(first_line_end + 1, closing_fence - first_line_end - 1);
    return trimWhitespace(body);
}

std::vector<ParsedToolCall> parseToolCalls(const std::string& response) {
    std::vector<ParsedToolCall> calls;

    // Accept current full-width tags, variants with a trailing full-width bar,
    // legacy full-width slash placement, and plain ASCII XML-style tags.
    const std::regex calls_start_re(R"(<(?:｜)?tool_calls(?:｜)?\s*>)");
    const std::regex calls_end_re(R"((?:</(?:｜)?tool_calls(?:｜)?\s*>|<｜/tool_calls(?:｜)?\s*>))");

    std::smatch start_match;
    if (!std::regex_search(response, start_match, calls_start_re)) {
        return calls;
    }

    const size_t content_start = static_cast<size_t>(start_match.position() + start_match.length());
    std::string after_start = response.substr(content_start);
    std::smatch end_match;
    if (!std::regex_search(after_start, end_match, calls_end_re)) {
        return calls;
    }

    std::string tools_content = after_start.substr(0, static_cast<size_t>(end_match.position()));

    const std::regex tool_start_re(R"(<(?:｜)?tool_call(?:｜)?\s+[^>]*name\s*=\s*(['"])(.*?)\1[^>]*>)");
    const std::regex tool_end_re(R"((?:</(?:｜)?tool_call(?:｜)?\s*>|<｜/tool_call(?:｜)?\s*>))");

    std::string::const_iterator search_begin = tools_content.cbegin();
    while (search_begin != tools_content.cend()) {
        std::smatch tool_start_match;
        if (!std::regex_search(search_begin, tools_content.cend(), tool_start_match, tool_start_re)) {
            break;
        }

        const auto body_begin = tool_start_match.suffix().first;
        std::smatch tool_end_match;
        if (!std::regex_search(body_begin, tools_content.cend(), tool_end_match, tool_end_re)) {
            break;
        }

        calls.push_back({tool_start_match[2].str(), stripMarkdownFence(std::string(body_begin, tool_end_match.prefix().second))});
        search_begin = tool_end_match.suffix().first;
    }

    return calls;
}

bool runToolCallParserChecks() {
    const std::vector<std::pair<std::string, ParsedToolCall>> checks = {
        {
            "<｜tool_calls｜><｜tool_call name=\"run_terminal\">{\"command\":\"echo current\"}</｜tool_call></｜tool_calls｜>",
            {"run_terminal", "{\"command\":\"echo current\"}"}
        },
        {
            "<tool_calls><tool_call name=\"read_file\">{\"path\":\"README.md\"}</tool_call></tool_calls>",
            {"read_file", "{\"path\":\"README.md\"}"}
        },
        {
            "<tool_calls><tool_call   name='terminal'>{\"command\":\"pwd\"}</tool_call></tool_calls>",
            {"terminal", "{\"command\":\"pwd\"}"}
        },
        {
            "<tool_calls><tool_call name=\"execute_command\">```json\n{\"command\":\"echo fenced\"}\n```</tool_call></tool_calls>",
            {"execute_command", "{\"command\":\"echo fenced\"}"}
        }
    };

    for (const auto& check : checks) {
        auto parsed = parseToolCalls(check.first);
        if (parsed.size() != 1 || parsed[0].name != check.second.name || parsed[0].body != check.second.body) {
            std::cerr << "Tool call parser check failed for input: " << check.first << std::endl;
            return false;
        }
    }

    std::cout << "Tool call parser checks passed\n";
    return true;
}

} // namespace

// 执行单个工具调用并返回结果
std::string executeSingleToolCall(const std::string& tool_name, const std::string& params_raw, const std::string& work_dir) {
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
            return "Error: Missing 'path' parameter";
        }
        
        // 如果是相对路径，拼接工作目录
        if (path[0] != '/') {
            path = work_dir + "/" + path;
        }
        
        if (writeFile(path, content)) {
            return "Success: File written to " + path;
        } else {
            return "Error: Failed to write file";
        }
        
    } else if (tool_name == "read_file") {
        std::string path = extractParam("path");
        if (path.empty()) {
            return "Error: Missing 'path' parameter";
        }
        if (path[0] != '/') {
            path = work_dir + "/" + path;
        }
        
        std::string content = readFile(path);
        if (!content.empty()) {
            return "Success:\n" + content;
        } else {
            return "Error: Failed to read file or file not found";
        }
        
    } else if (tool_name == "edit_file") {
        std::string path = extractParam("path");
        std::string old_text = extractParam("old_text");
        std::string new_text = extractParam("new_text");
        
        if (path.empty()) {
            return "Error: Missing 'path' parameter";
        }
        if (path[0] != '/') {
            path = work_dir + "/" + path;
        }
        
        std::string content = readFile(path);
        if (!content.empty() && !old_text.empty()) {
            size_t pos_found = content.find(old_text);
            if (pos_found != std::string::npos) {
                content.replace(pos_found, old_text.length(), new_text);
                if (writeFile(path, content)) {
                    return "Success: File edited";
                } else {
                    return "Error: Failed to save edited file";
                }
            } else {
                return "Error: Old text not found in file";
            }
        } else {
            return "Error: Failed to read file or empty old_text";
        }
        
    } else if (tool_name == "run_terminal" || tool_name == "terminal" || tool_name == "execute_command") {
        std::string command = extractParam("command");
        
        if (!command.empty()) {
            std::string output = executeCommand(command);
            if (!output.empty()) {
                return "Command output:\n" + output;
            } else {
                return "Command executed (no output)";
            }
        } else {
            return "Error: Missing 'command' parameter";
        }
        
    } else if (tool_name == "list_directory") {
        std::string path = extractParam("path");
        if (path.empty()) path = work_dir;
        if (path[0] != '/') {
            path = work_dir + "/" + path;
        }
        
        std::string cmd = "ls -la \"" + path + "\"";
        std::string output = executeCommand(cmd);
        if (!output.empty()) {
            return "Directory listing:\n" + output;
        } else {
            return "Error: Failed to list directory";
        }
    } else {
        return "Error: Unknown tool: " + tool_name;
    }
}

// 解析并执行工具调用
void executeToolCalls(const std::string& response, const std::string& work_dir) {
    std::vector<ParsedToolCall> tool_calls = parseToolCalls(response);
    if (tool_calls.empty()) {
        return;
    }

    for (const auto& tool_call : tool_calls) {
        std::string tool_name = tool_call.name;
        std::string params_raw = tool_call.body;
        
        std::cout << "\n🔧 Executing tool: " << tool_name << std::endl;
        
        std::string result = executeSingleToolCall(tool_name, params_raw, work_dir);
        std::cout << "   " << result << "\n";
    }
}


int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--self-test-tool-parser") {
        return runToolCallParserChecks() ? 0 : 1;
    }
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
    std::string selected_model;
    bool use_previous_api = false;
    int provider_choice = ui.showConfigWizard(config_mgr, selected_model, use_previous_api);
    
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
    
    // 标志位：是否需要用户输入（首次进入循环或工具调用完成后）
    bool need_user_input = true;
    std::string pending_user_input;
    
    while (!g_interrupted) {
        std::string user_input;
        
        // 获取用户输入（仅在需要时）
        if (need_user_input) {
            user_input = ui.getInput("You");
            
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
            pending_user_input.clear();
        } else {
            // 工具调用完成后，自动继续，不需要用户输入
            need_user_input = true;  // 下次循环需要用户输入
            continue;  // 直接进入下一轮请求
        }
        
        // 发送请求
        std::vector<nlohmann::json> tools_schema = mcp_mgr.getToolsSchema();
        std::string provider_str = ConfigManager::providerToString(provider);
        
        ChatResponse response = http_client.sendChatRequest(
            api_url,
            api_key,
            model,
            messages,
            provider_str,
            tools_schema
        );
        
        if (!response.success) {
            ui.showError("API Error: " + response.error_message);
            messages.pop_back();  // 移除失败的用户消息
            continue;
        }
        
        // 显示 AI 响应
        ui.showAIResponse(response.content);
        
        // 执行原生 tool_calls (OpenAI/DeepSeek/Anthropic 格式)
        if (!response.tool_calls.empty()) {
            // 首先保存包含 tool_calls 的助手消息到历史记录
            ChatMessage assistant_msg;
            assistant_msg.role = "assistant";
            assistant_msg.content = response.content;
            assistant_msg.tool_calls = response.tool_calls;
            messages.push_back(assistant_msg);
            
            std::cout << "\n🔧 Executing " << response.tool_calls.size() << " native tool call(s)..." << std::endl;
            
            // 存储工具执行结果
            std::vector<nlohmann::json> tool_results;
            
            for (const auto& tc : response.tool_calls) {
                std::cout << "   - " << tc.name << std::endl;
                nlohmann::json args = tc.arguments.is_null() ? tc.input : tc.arguments;
                nlohmann::json result = mcp_mgr.executeTool(tc.name, args);
                tool_results.push_back(result);
                
                if (result.contains("error")) {
                    std::cout << "   ❌ Error: " << result["error"].get<std::string>() << std::endl;
                } else if (result.contains("output")) {
                    std::cout << "   ✅ Result: " << result["output"].get<std::string>() << std::endl;
                } else if (result.contains("content")) {
                    std::cout << "   ✅ Content:\n" << result["content"].get<std::string>() << std::endl;
                } else {
                    std::cout << "   ✅ Success" << std::endl;
                }
            }
            
            // 添加工具调用结果到消息历史
            if (provider == ProviderType::Anthropic) {
                // Anthropic 格式：使用 content_blocks
                nlohmann::json content_blocks = nlohmann::json::array();
                for (size_t i = 0; i < response.tool_calls.size(); i++) {
                    const auto& tc = response.tool_calls[i];
                    const auto& result = tool_results[i];
                    
                    nlohmann::json tool_result_block;
                    tool_result_block["type"] = "tool_result";
                    tool_result_block["tool_use_id"] = tc.id;
                    if (result.contains("error")) {
                        tool_result_block["content"] = result["error"].get<std::string>();
                        tool_result_block["is_error"] = true;
                    } else if (result.contains("output")) {
                        tool_result_block["content"] = result["output"].get<std::string>();
                    } else if (result.contains("content")) {
                        tool_result_block["content"] = result["content"].get<std::string>();
                    } else {
                        tool_result_block["content"] = "Success";
                    }
                    content_blocks.push_back(tool_result_block);
                }
                
                ChatMessage tool_result_msg;
                tool_result_msg.role = "user";
                tool_result_msg.content = "";
                tool_result_msg.content_blocks = content_blocks;
                messages.push_back(tool_result_msg);
            } else {
                // OpenAI/DeepSeek 格式：使用 tool role
                for (size_t i = 0; i < response.tool_calls.size(); i++) {
                    const auto& tc = response.tool_calls[i];
                    const auto& result = tool_results[i];
                    
                    std::string result_content;
                    if (result.contains("error")) {
                        result_content = "Error: " + result["error"].get<std::string>();
                    } else if (result.contains("output")) {
                        result_content = result["output"].get<std::string>();
                    } else if (result.contains("content")) {
                        result_content = result["content"].get<std::string>();
                    } else {
                        result_content = "Success";
                    }
                    
                    ChatMessage tool_msg;
                    tool_msg.role = "tool";
                    tool_msg.content = result_content;
                    tool_msg.tool_call_id = tc.id;  // 添加 tool_call_id
                    messages.push_back(tool_msg);
                }
            }
            
            // 记录 token 使用
            token_tracker.recordTokens(response.input_tokens, response.output_tokens);
            
            // 显示本次费用
            ModelPricing pricing = TokenTracker::getModelPrice(model);
            double this_cost = (static_cast<double>(response.input_tokens) / 1000000.0) * pricing.input_price_per_1m +
                              (static_cast<double>(response.output_tokens) / 1000000.0) * pricing.output_price_per_1m;
            
            if (this_cost > 0.000001) {
                std::cout << "   💰 This turn: $" << std::fixed << std::setprecision(6) << this_cost << " USD\n";
            }
            
            // 显示累计统计
            std::cout << "   📊 Session total: "
                     << token_tracker.getTotalInputTokens() + token_tracker.getTotalOutputTokens()
                     << " tokens, $" << std::fixed << std::setprecision(6)
                     << token_tracker.getTotalCostUSD() << " USD\n\n";
            
            // 标记为不需要用户输入，自动继续
            need_user_input = false;
            continue;
        }
        
        // 解析并执行 XML 格式的工具调用 (回退方案)
        std::vector<ParsedToolCall> xml_tool_calls = parseToolCalls(response.content);
        if (!xml_tool_calls.empty()) {
            // 首先保存包含 XML 工具调用的助手消息到历史记录
            ChatMessage assistant_msg_xml;
            assistant_msg_xml.role = "assistant";
            assistant_msg_xml.content = response.content;
            messages.push_back(assistant_msg_xml);
            
            // 执行 XML 工具调用并收集结果
            std::vector<std::string> tool_outputs;
            for (const auto& tool_call : xml_tool_calls) {
                std::string output = executeSingleToolCall(tool_call.name, tool_call.body, work_dir);
                tool_outputs.push_back(output);
            }
            
            // 将工具结果添加回消息历史 (作为 user 消息，因为 XML 回退不是标准协议)
            std::string combined_output;
            for (size_t i = 0; i < xml_tool_calls.size(); i++) {
                if (i > 0) combined_output += "\n\n";
                combined_output += "Tool: " + xml_tool_calls[i].name + "\nResult: " + tool_outputs[i];
            }
            
            ChatMessage tool_result_msg;
            tool_result_msg.role = "user";
            tool_result_msg.content = combined_output;
            messages.push_back(tool_result_msg);
            
            // 记录 token 使用
            token_tracker.recordTokens(response.input_tokens, response.output_tokens);
            
            // 显示本次费用
            ModelPricing pricing = TokenTracker::getModelPrice(model);
            double this_cost = (static_cast<double>(response.input_tokens) / 1000000.0) * pricing.input_price_per_1m +
                              (static_cast<double>(response.output_tokens) / 1000000.0) * pricing.output_price_per_1m;
            
            if (this_cost > 0.000001) {
                std::cout << "   💰 This turn: $" << std::fixed << std::setprecision(6) << this_cost << " USD\n";
            }
            
            // 显示累计统计
            std::cout << "   📊 Session total: "
                     << token_tracker.getTotalInputTokens() + token_tracker.getTotalOutputTokens()
                     << " tokens, $" << std::fixed << std::setprecision(6)
                     << token_tracker.getTotalCostUSD() << " USD\n\n";
            
            // 继续循环，让模型基于工具结果生成下一轮响应
            continue;
        }
        
        // 没有工具调用，正常显示响应
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
