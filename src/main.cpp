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
#include <chrono>

#include "config_manager.hpp"
#include "token_tracker.hpp"
#include "git_manager.hpp"
#include "tool_registry.hpp"
#include "http_client.hpp"
#include "ui.hpp"
#include "prompt.hpp"
#include "tool_policy.hpp"
#include "skill_manager.hpp"
#include "change_journal.hpp"
#include "session_manager.hpp"
#include "secret_store.hpp"
#include "endpoint_policy.hpp"
#include "agent_session.hpp"
#include "model_provider.hpp"
#include "sandbox_runner.hpp"
#include "subagent_runner.hpp"
#include "session_controls.hpp"
#include "mcp_client.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {
void initConsole() {
#if defined(_WIN32)
    // 设置控制台输入/输出为 UTF-8，解决中文乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // 启用 ANSI 转义序列（Windows 10+ 支持）
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, mode);
        }
    }
#else
    // macOS/Linux: 确保 locale 设置为 UTF-8
    std::setlocale(LC_ALL, "en_US.UTF-8");
#endif
}
} // namespace

using namespace opencode;

volatile std::sig_atomic_t g_interrupted = 0;
volatile std::sig_atomic_t g_operation_active = 0;
volatile std::sig_atomic_t g_cancel_requested = 0;
CancellationToken g_cancellation(&g_cancel_requested);

void signalHandler(int /* signum */) {
    if (g_operation_active) {
        if (g_cancel_requested) {
            g_interrupted = 1;
        } else {
            g_cancel_requested = 1;
        }
    } else {
        g_interrupted = 1;
    }
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

int main(int argc, char* argv[]) {
    initConsole();

    // Parse subcommand.
    std::string subcommand;
    std::string exec_prompt;
    bool exec_json = false;
    std::string resume_id;
    bool doctor_mode = false;
    std::string model_override;
    std::string subagent_model_override;

    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            std::cout << "Turtle.AI AgentCLI\n\n"
                      << "Usage:\n"
                      << "  Turtle.AI_AgentCLI                       Interactive chat\n"
                      << "  Turtle.AI_AgentCLI exec [--json] <prompt>  Run one prompt, print result, exit\n"
                      << "  Turtle.AI_AgentCLI resume <session-id>     Resume a saved session\n"
                      << "  Turtle.AI_AgentCLI doctor                  Check system readiness\n"
                      << "  --model <name>                             Override main model\n"
                      << "  --subagent-model <name>                    Enable/override sub-agent model\n"
                      << "  Turtle.AI_AgentCLI --self-test-tool-parser  Run parser self-check\n"
                      << "  Turtle.AI_AgentCLI --help                   Show this message\n";
            return 0;
        }
        if (arg == "--self-test-tool-parser") {
            return runToolCallParserChecks() ? 0 : 1;
        }
        if (arg == "--model" && i + 1 < argc) {
            model_override = argv[++i];
            continue;
        }
        if (arg == "--subagent-model" && i + 1 < argc) {
            subagent_model_override = argv[++i];
            continue;
        }
        if (arg == "exec") {
            subcommand = "exec";
            continue;
        }
        if (arg == "--json" && subcommand == "exec") {
            exec_json = true;
            continue;
        }
        if (arg == "resume") {
            subcommand = "resume";
            if (i + 1 < argc) resume_id = argv[++i];
            continue;
        }
        if (arg == "doctor") {
            doctor_mode = true;
            continue;
        }
        if (subcommand == "exec" && exec_prompt.empty()) {
            exec_prompt = arg;
            continue;
        }
    }

    // ─── Doctor mode: check system readiness ───────────────────────────
    if (doctor_mode) {
        std::cout << "Turtle.AI AgentCLI — System Doctor\n\n";
        bool ok = true;

        // Check sandbox.
        std::cout << "  Sandbox available: "
                  << (SandboxRunner::isAvailable() ? "yes" : "no") << '\n';
        if (!SandboxRunner::isAvailable()) ok = false;

        // Check config.
        ConfigManager cm;
        if (cm.loadDefaultConfig()) {
            std::cout << "  Configuration:     loaded\n";
        } else {
            std::cout << "  Configuration:     missing or invalid\n";
            ok = false;
        }

        // Check network.
        HttpClient hc;
        std::cout << "  HTTP transport:    initialized\n";

        std::cout << '\n' << (ok ? "System ready." : "Some checks failed.") << '\n';
        return ok ? 0 : 2;
    }

    // ─── Resume mode: non-interactive resume ───────────────────────────
    if (subcommand == "resume" && !resume_id.empty()) {
        // Non-interactive resume: load session, run one turn, exit.
        UI ui;
        ConfigManager config_mgr;
        if (!config_mgr.loadDefaultConfig() || !config_mgr.validate()) {
            std::cerr << "No valid configuration found. Run the interactive "
                         "mode first to set up your provider and API key.\n";
            return 2;
        }
        auto& config = config_mgr.getConfig();
        if (!model_override.empty()) config.model = model_override;
        if (!subagent_model_override.empty()) {
            config.subagent_enabled = true;
            config.subagent_provider = config.provider;
            config.subagent_api_url = config.api_url;
            config.subagent_model = subagent_model_override;
        }
        auto secret_store = createSecretStore();
        std::string api_key;
        const std::string provider_name = ConfigManager::providerToString(config.provider);
        if (const auto stored = secret_store->get(provider_name)) {
            api_key = stored->value;
        }

        HttpClient http_client;
        TokenTracker token_tracker;
        token_tracker.setModel(config.model);
        ToolRegistry tool_registry;
        ChangeJournal change_journal;
        SessionManager session_mgr;
        session_mgr.setWorkspace(std::filesystem::weakly_canonical(config.work_dir));
        tool_registry.setChangeJournal(&change_journal);
        tool_registry.setWorkspaceRoot(config.work_dir);

        std::vector<ChatMessage> messages;
        SessionInfo info;
        std::string load_error;
        if (!session_mgr.load(resume_id, messages, info, &load_error)) {
            std::cerr << "Cannot resume session: " << load_error << '\n';
            return 3;
        }
        std::cout << "Resumed session: " << info.title
                  << " (" << info.message_count << " messages)\n";

        auto model_provider = createModelProvider(
            provider_name, http_client, config.api_url, api_key, config.model
        );
        std::unique_ptr<ModelProvider> subagent_provider;
        std::unique_ptr<SubagentRunner> subagent_runner;
        if (config.subagent_enabled) {
            auto subagent_tools = tool_registry.getToolsSchema();
            subagent_provider = createModelProvider(
                ConfigManager::providerToString(config.subagent_provider),
                http_client,
                config.subagent_api_url,
                api_key,
                config.subagent_model
            );
            subagent_runner = std::make_unique<SubagentRunner>(
                *subagent_provider,
                std::move(subagent_tools),
                [&](const std::string& name, const nlohmann::json& args) {
                    return tool_registry.executeTool(name, args);
                },
                [](const ToolApproval&) { return false; }
            );
            tool_registry.registerTool({
                "delegate_task",
                "Delegate one focused task to the configured sub-agent",
                [&](const nlohmann::json& args) {
                    return subagent_runner->run({
                        args.value("task", ""),
                        args.value("role", ""),
                        args.value("context", "")
                    });
                },
                {
                    {"type", "object"},
                    {"properties", {
                        {"task", {{"type", "string"}}},
                        {"role", {{"type", "string"}}},
                        {"context", {{"type", "string"}}}
                    }},
                    {"required", nlohmann::json::array({"task"})},
                    {"additionalProperties", false}
                }
            });
        }
        AgentSessionCallbacks cb;
        cb.execute_tool = [&](const std::string& name, const nlohmann::json& args) {
            return tool_registry.executeTool(name, args);
        };
        cb.approve_tool = [](const ToolApproval&) { return false; }; // no approval in non-interactive
        cb.emit = [](const AgentEvent&) {};
        AgentSession session(*model_provider, messages, tool_registry.getToolsSchema(), token_tracker, std::move(cb));
        auto result = session.runTurn(exec_prompt.empty() ? "Continue" : exec_prompt);
        std::cout << (result.success ? "OK" : "FAILED: " + result.error) << '\n';
        return result.success ? 0 : 1;
    }

    // ─── Exec mode: non-interactive single turn ────────────────────────
    if (subcommand == "exec" && !exec_prompt.empty()) {
        UI ui;
        ConfigManager config_mgr;
        if (!config_mgr.loadDefaultConfig() || !config_mgr.validate()) {
            std::cerr << "No valid configuration found. Run the interactive "
                         "mode first to set up your provider and API key.\n";
            return 2;
        }
        auto& config = config_mgr.getConfig();
        if (!model_override.empty()) config.model = model_override;
        if (!subagent_model_override.empty()) {
            config.subagent_enabled = true;
            config.subagent_provider = config.provider;
            config.subagent_api_url = config.api_url;
            config.subagent_model = subagent_model_override;
        }
        auto secret_store = createSecretStore();
        std::string api_key;
        const std::string provider_name = ConfigManager::providerToString(config.provider);
        if (const auto stored = secret_store->get(provider_name)) {
            api_key = stored->value;
        }
        if (api_key.empty()) {
            const char* env = nullptr;
            if (config.provider == ProviderType::OpenAI) env = std::getenv("OPENAI_API_KEY");
            else if (config.provider == ProviderType::Anthropic) env = std::getenv("ANTHROPIC_API_KEY");
            else if (config.provider == ProviderType::DeepSeek) env = std::getenv("DEEPSEEK_API_KEY");
            if (env) api_key = env;
        }

        HttpClient http_client;
        TokenTracker token_tracker;
        token_tracker.setModel(config.model);
        ToolRegistry tool_registry;
        ChangeJournal change_journal;
        tool_registry.setChangeJournal(&change_journal);
        tool_registry.setWorkspaceRoot(config.work_dir);

        PromptManager prompt_mgr;
        std::vector<ChatMessage> messages = {
            {"system", prompt_mgr.getSystemPrompt(), nullptr, "", {}}
        };

        auto model_provider = createModelProvider(
            provider_name, http_client, config.api_url, api_key, config.model
        );
        std::unique_ptr<ModelProvider> subagent_provider;
        std::unique_ptr<SubagentRunner> subagent_runner;
        if (config.subagent_enabled) {
            auto subagent_tools = tool_registry.getToolsSchema();
            subagent_provider = createModelProvider(
                ConfigManager::providerToString(config.subagent_provider),
                http_client,
                config.subagent_api_url,
                api_key,
                config.subagent_model
            );
            subagent_runner = std::make_unique<SubagentRunner>(
                *subagent_provider,
                std::move(subagent_tools),
                [&](const std::string& name, const nlohmann::json& args) {
                    return tool_registry.executeTool(name, args);
                },
                [](const ToolApproval&) { return false; }
            );
            tool_registry.registerTool({
                "delegate_task",
                "Delegate one focused task to the configured sub-agent",
                [&](const nlohmann::json& args) {
                    return subagent_runner->run({
                        args.value("task", ""),
                        args.value("role", ""),
                        args.value("context", "")
                    });
                },
                {
                    {"type", "object"},
                    {"properties", {
                        {"task", {{"type", "string"}}},
                        {"role", {{"type", "string"}}},
                        {"context", {{"type", "string"}}}
                    }},
                    {"required", nlohmann::json::array({"task"})},
                    {"additionalProperties", false}
                }
            });
        }

        int exit_code = 0;
        AgentSessionCallbacks cb;
        cb.execute_tool = [&](const std::string& name, const nlohmann::json& args) {
            return tool_registry.executeTool(name, args);
        };
        cb.approve_tool = [](const ToolApproval&) { return false; };

        if (exec_json) {
            // JSONL output to stdout.
            cb.emit = [](const AgentEvent& evt) {
                nlohmann::json j;
                j["type"] = static_cast<int>(evt.type);
                j["text"] = evt.text;
                j["tool_name"] = evt.tool_name;
                j["detail"] = evt.detail;
                j["success"] = evt.success;
                j["input_tokens"] = evt.input_tokens;
                j["output_tokens"] = evt.output_tokens;
                j["turn_cost_usd"] = evt.turn_cost_usd;
                j["total_cost_usd"] = evt.total_cost_usd;
                std::cout << j.dump() << std::endl;
            };
        } else {
            cb.emit = [](const AgentEvent& evt) {
                if (evt.type == AgentEventType::ModelTextDelta)
                    std::cout << evt.text << std::flush;
                else if (evt.type == AgentEventType::ToolRequested)
                    std::cerr << "[tool] " << evt.tool_name << ": " << evt.detail << '\n';
                else if (evt.type == AgentEventType::TurnFailed)
                    std::cerr << "[error] " << evt.text << '\n';
            };
        }

        AgentSession session(*model_provider, messages, tool_registry.getToolsSchema(), token_tracker, std::move(cb));
        auto result = session.runTurn(exec_prompt);

        if (!exec_json) std::cout << '\n';

        if (!result.success) {
            std::cerr << "Error: " << result.error << '\n';
            exit_code = 1;
        }

        return exit_code;
    }

    // 设置信号处理
    std::signal(SIGINT, signalHandler);
    
    UI ui;
    ConfigManager config_mgr;
    TokenTracker token_tracker;
    GitManager git_mgr;
    ToolRegistry tool_registry;
    SkillManager skill_mgr;
    ChangeJournal change_journal;
    SessionManager session_mgr;
    HttpClient http_client;
    PromptManager prompt_mgr;
    std::unique_ptr<SubagentRunner> subagent_runner;
    auto secret_store = createSecretStore();
    config_mgr.loadDefaultConfig();
    http_client.setCancellationToken(&g_cancellation);
    tool_registry.setCancellationToken(&g_cancellation);
    
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
            break;
        case 2:  // OpenAI
            provider = ProviderType::OpenAI;
            api_url = "https://api.openai.com/v1/chat/completions";
            break;
        case 3:  // Anthropic
            provider = ProviderType::Anthropic;
            api_url = "https://api.anthropic.com/v1/messages";
            break;
        case 4:  // LlamaCpp
            provider = ProviderType::LlamaCpp;
            api_url = "http://localhost:8080/v1/chat/completions";
            break;
        case 5:  // Custom OpenAI-compatible
            provider = ProviderType::OpenAICompatible;
            api_url = ui.getInput(
                "OpenAI-compatible chat completions endpoint URL"
            );
            break;
        default:
            ui.showError("Invalid provider choice");
            return 1;
    }
    model = ui.getInput(
        "Main model name (exact name supported by your API)"
    );
    if (model.empty()) {
        ui.showError("Main model name is required");
        return 1;
    }
    
    // 获取 API Key (本地模式可选)
    if (provider == ProviderType::OpenAICompatible) {
        api_key = ui.getSecretInput(
            "API key for custom endpoint (optional, input hidden)"
        );
    } else if (provider != ProviderType::LlamaCpp) {
        const std::string provider_name =
            ConfigManager::providerToString(provider);
        bool offer_persistent_store = false;
        bool credential_from_legacy_file = false;
        bool credential_secured = false;
        if (const auto stored = secret_store->get(provider_name)) {
            api_key = stored->value;
            ui.showMessage("Credentials", "Using " + stored->source);
        } else if (const auto legacy = config_mgr.takeLegacyApiKey(provider)) {
            api_key = *legacy;
            offer_persistent_store = true;
            credential_from_legacy_file = true;
            ui.showMessage(
                "Credentials",
                "Migrated credential from legacy config into process memory"
            );
        } else {
            api_key = ui.getSecretInput("Enter your API key (input hidden)");
            offer_persistent_store = true;
        }
        if (api_key.empty()) {
            ui.showError("API key is required");
            return 1;
        }
        if (offer_persistent_store &&
            secret_store->supportsPersistentStore()) {
            const std::string save_key =
                ui.getInput("Store this credential in macOS Keychain? (y/n)");
            if (save_key == "y" || save_key == "Y") {
                std::string store_error;
                if (!secret_store->store(
                        provider_name, api_key, &store_error
                    )) {
                    ui.showError("Could not store credential: " + store_error);
                } else {
                    credential_secured = true;
                    ui.showMessage(
                        "Credentials",
                        "Stored securely in macOS Keychain"
                    );
                }
            }
        }
        if (credential_from_legacy_file) {
            bool remove_plaintext = credential_secured;
            if (!remove_plaintext) {
                const std::string choice = ui.getInput(
                    "Remove the plaintext legacy credential now? "
                    "(it will remain only for this process) (y/n)"
                );
                remove_plaintext = choice == "y" || choice == "Y";
            }
            if (remove_plaintext) {
                if (!config_mgr.sanitizeLegacyCredentials()) {
                    ui.showError(
                        "Could not fully remove the legacy plaintext credential"
                    );
                } else {
                    ui.showMessage(
                        "Credentials",
                        "Removed plaintext credential from legacy files"
                    );
                }
            } else {
                ui.showMessage(
                    "Credentials",
                    "Legacy plaintext credential was left unchanged"
                );
            }
        } else if (config_mgr.hasLegacyApiKey()) {
            const std::string choice = ui.getInput(
                "A plaintext credential remains in a legacy configuration. "
                "Remove it now? (y/n)"
            );
            if (choice == "y" || choice == "Y") {
                if (config_mgr.sanitizeLegacyCredentials()) {
                    ui.showMessage(
                        "Credentials",
                        "Removed plaintext credential from legacy files"
                    );
                } else {
                    ui.showError(
                        "Could not fully remove the legacy plaintext credential"
                    );
                }
            }
        }
    } else {
        api_key = "";  // 本地模式不需要 API key
    }

    const auto endpoint_decision = EndpointPolicy::assess(
        ConfigManager::providerToString(provider),
        api_url,
        !api_key.empty()
    );
    if (!endpoint_decision.allowed) {
        ui.showError(endpoint_decision.error);
        return 1;
    }
    if (endpoint_decision.credential_confirmation_required) {
        const std::string confirmed = ui.getInput(
            "Send credential to host '" + endpoint_decision.host + "'? (y/n)"
        );
        if (confirmed != "y" && confirmed != "Y") {
            ui.showError("Credential transmission was not approved");
            return 1;
        }
    }
    
    // 确认模型
    std::cout << "\nUsing model: " << model << "\n";
    bool subagent_enabled = false;
    std::string subagent_model;
    const std::string enable_subagent =
        ui.getInput("Enable a sub-agent? (y/n)");
    if (enable_subagent == "y" || enable_subagent == "Y") {
        subagent_model = ui.getInput(
            "Sub-agent model name (exact name; Enter reuses the main model)"
        );
        if (subagent_model.empty()) subagent_model = model;
        subagent_enabled = true;
        std::cout << "Using sub-agent model: " << subagent_model << "\n";
    }
    
    // 设置 token 追踪
    token_tracker.setModel(model);
    
    // 选择工作目录
    std::string work_dir = ui.getInput("Enter working directory (default: current)");
    if (work_dir.empty()) {
        work_dir = ".";
    }
    if (!tool_registry.setWorkspaceRoot(work_dir)) {
        ui.showError("Working directory does not exist or is not accessible");
        return 1;
    }
    tool_registry.setChangeJournal(&change_journal);
    std::error_code workspace_error;
    const auto workspace_root =
        std::filesystem::weakly_canonical(work_dir, workspace_error);
    if (workspace_error) {
        ui.showError("Working directory cannot be resolved");
        return 1;
    }
    if (!session_mgr.setWorkspace(workspace_root)) {
        ui.showError("Cannot initialize session storage for this workspace");
        return 1;
    }
    MemoryStore memory_store(workspace_root);
    memory_store.rememberProject();
    GoalState goal_state;
    std::size_t goal_max_continuations = 63;
    KeepAwakeGuard keep_awake;
    McpRegistry mcp_registry;
    std::string requested_session_id;
    const auto saved_sessions = session_mgr.list();
    if (!saved_sessions.empty()) {
        std::cout << "\n[Saved sessions]\n";
        const std::size_t shown = std::min<std::size_t>(saved_sessions.size(), 10);
        for (std::size_t i = 0; i < shown; ++i) {
            const auto& session = saved_sessions[i];
            std::cout << "  " << session.id << "  "
                      << session.title << "  [" << session.message_count
                      << " messages]\n";
        }
        requested_session_id = ui.getInput(
            "Session ID to resume (Enter for new)"
        );
    }

    std::vector<std::filesystem::path> skill_roots;
    std::error_code skill_path_error;
    const auto executable = std::filesystem::weakly_canonical(argv[0], skill_path_error);
    if (!skill_path_error) {
        skill_roots.push_back(executable.parent_path().parent_path() / "skills");
    }
    skill_roots.push_back(std::filesystem::current_path() / "skills");
    if (const char* home = std::getenv("HOME")) {
        skill_roots.push_back(std::filesystem::path(home) / ".turtle" / "skills");
    }
    skill_roots.push_back(std::filesystem::path(work_dir) / ".turtle" / "skills");
    skill_mgr.discover(skill_roots);

    const nlohmann::json no_parameters = {
        {"type", "object"},
        {"properties", nlohmann::json::object()},
        {"additionalProperties", false}
    };
    tool_registry.registerTool({
        "list_skills",
        "List available task-specific skills and their trigger descriptions",
        [&](const nlohmann::json&) { return skill_mgr.listSkills(); },
        no_parameters
    });
    tool_registry.registerTool({
        "manage_skill",
        "Create, replace, or delete a workspace-local skill. Use only when the user asks to manage skills.",
        [&](const nlohmann::json& args) {
            auto result =
                skill_mgr.manageSkill(args, workspace_root, change_journal);
            if (result.value("success", false)) skill_mgr.discover(skill_roots);
            return result;
        },
        {
            {"type", "object"},
            {"properties", {
                {"action", {
                    {"type", "string"},
                    {"enum", nlohmann::json::array({"upsert", "delete"})}
                }},
                {"name", {{"type", "string"}}},
                {"content", {
                    {"type", "string"},
                    {"description", "Complete SKILL.md; required for upsert"}
                }}
            }},
            {"required", nlohmann::json::array({"action", "name"})},
            {"additionalProperties", false}
        }
    });
    tool_registry.registerTool({
        "load_skill",
        "Load the complete instructions for one available skill",
        [&](const nlohmann::json& args) {
            if (!args.contains("name") || !args["name"].is_string()) {
                return nlohmann::json({
                    {"success", false}, {"error", "Missing string argument: name"}
                });
            }
            return skill_mgr.loadSkill(args["name"].get<std::string>());
        },
        {
            {"type", "object"},
            {"properties", {{"name", {{"type", "string"}}}}},
            {"required", nlohmann::json::array({"name"})},
            {"additionalProperties", false}
        }
    });
    tool_registry.registerTool({
        "delegate_task",
        "Delegate one focused task to the configured sub-agent",
        [&](const nlohmann::json& args) {
            if (!subagent_runner) {
                return nlohmann::json({
                    {"success", false},
                    {"error", "No sub-agent is configured"}
                });
            }
            auto result = subagent_runner->run({
                args.value("task", ""),
                args.value("role", ""),
                args.value("context", "")
            });
            ui.showMessage(
                "Sub-agent usage",
                subagent_model + ": +" +
                    std::to_string(result.value("input_tokens", 0)) +
                    " input, +" +
                    std::to_string(result.value("output_tokens", 0)) +
                    " output; cumulative $" +
                    std::to_string(
                        subagent_runner->totalCostUSD()
                    )
            );
            return result;
        },
        {
            {"type", "object"},
            {"properties", {
                {"task", {{"type", "string"}}},
                {"role", {{"type", "string"}}},
                {"context", {{"type", "string"}}}
            }},
            {"required", nlohmann::json::array({"task"})},
            {"additionalProperties", false}
        }
    });
    tool_registry.registerTool({
        "goal_status",
        "Read the active goal and subtask progress",
        [&](const nlohmann::json&) {
            nlohmann::json tasks = nlohmann::json::array();
            const auto& values = goal_state.subtasks();
            for (std::size_t i = 0; i < values.size(); ++i) {
                tasks.push_back({
                    {"index", i + 1},
                    {"text", values[i].text},
                    {"completed", values[i].completed}
                });
            }
            return nlohmann::json({
                {"success", true},
                {"active", goal_state.active()},
                {"goal", goal_state.text()},
                {"subtasks", std::move(tasks)}
            });
        },
        no_parameters
    });
    tool_registry.registerTool({
        "update_subtask",
        "Mark one active-goal subtask complete after verifying its deliverable",
        [&](const nlohmann::json& args) {
            const std::size_t index = args.value("index", 0u);
            const bool completed = args.value("completed", true);
            if (!completed) {
                return nlohmann::json({
                    {"success", false},
                    {"error", "Reopening completed subtasks is not supported"}
                });
            }
            const bool updated = goal_state.completeSubtask(index);
            return nlohmann::json({
                {"success", updated},
                {"index", index},
                {"error", updated ? "" : "Invalid subtask index"}
            });
        },
        {
            {"type", "object"},
            {"properties", {
                {"index", {{"type", "integer"}, {"minimum", 1}}},
                {"completed", {{"type", "boolean"}}}
            }},
            {"required", nlohmann::json::array({"index"})},
            {"additionalProperties", false}
        }
    });
    tool_registry.registerTool({
        "create_subtask",
        "Create a bounded subtask that supports and does not override the active goal",
        [&](const nlohmann::json& args) {
            if (!goal_state.active()) {
                return nlohmann::json({
                    {"success", false},
                    {"error", "No active goal"}
                });
            }
            const std::string text = trimWhitespace(args.value("text", ""));
            const bool created = goal_state.addSubtask(text);
            return nlohmann::json({
                {"success", created},
                {"index", goal_state.subtasks().size()},
                {"text", text},
                {"constraint", "The main goal remains authoritative"}
            });
        },
        {
            {"type", "object"},
            {"properties", {
                {"text", {
                    {"type", "string"},
                    {"description", "A bounded task supporting the active goal"}
                }}
            }},
            {"required", nlohmann::json::array({"text"})},
            {"additionalProperties", false}
        }
    });
    
    // Git 集成询问
    std::string use_git_str = ui.getInput("Enable Git integration? (y/n)");
    bool use_git = (use_git_str == "y" || use_git_str == "Y");
    
    // 检查 Git 状态
    if (use_git && git_mgr.isGitAvailable()) {
        GitStatus status = git_mgr.checkStatus(work_dir);
        if (status.is_repo) {
            std::cout << "\nGit repository detected: " << status.current_branch << "\n";
            if (status.has_uncommitted_changes) {
                std::cout << "Warning: You have " << status.modified_files.size() << " uncommitted file(s)\n";
            }
            if (status.is_behind_remote) {
                std::cout << "Warning: Local branch is " << status.commits_behind << " commit(s) behind remote\n";
            }
        } else {
            std::cout << "\nDirectory is not a Git repository\n";
        }
    }
    
    // 保存配置
    auto& config = config_mgr.getConfig();
    config.provider = provider;
    config.api_url = api_url;
    config.api_key = api_key;
    config.model = model;
    config.subagent_enabled = subagent_enabled;
    config.subagent_provider =
        subagent_enabled ? provider : ProviderType::Unknown;
    config.subagent_api_url = subagent_enabled ? api_url : "";
    config.subagent_model = subagent_model;
    config.work_dir = work_dir;
    config.use_git = use_git;
    if (!config_mgr.saveConfig(config_mgr.getDefaultConfigPath())) {
        ui.showError("Could not save non-secret configuration");
    }
    
    ui.clearScreen();
    ui.showConfigurationSummary(config);
    std::cout << "  Skills:    " << skill_mgr.size() << " loaded\n\n";
    
    // 对话循环
    std::vector<ChatMessage> messages;
    const std::string base_system_prompt =
        prompt_mgr.getSystemPrompt() + skill_mgr.getCatalogPrompt();
    auto currentSystemPrompt = [&]() {
        return base_system_prompt + memory_store.promptContext() +
               goal_state.prompt();
    };
    std::string current_system_prompt = currentSystemPrompt();
    std::string session_id;
    if (!requested_session_id.empty()) {
        SessionInfo resumed;
        std::string resume_error;
        if (session_mgr.load(
                requested_session_id, messages, resumed, &resume_error
            )) {
            if (!messages.empty() && messages.front().role == "system") {
                messages.front().content = current_system_prompt;
                messages.front().content_blocks = nullptr;
                messages.front().tool_calls.clear();
                messages.front().tool_call_id.clear();
            } else {
                messages.insert(messages.begin(), {
                    "system", current_system_prompt, nullptr, "", {}
                });
            }
            session_id = requested_session_id;
            ui.showMessage(
                "Session",
                "Resumed " + resumed.title + " (" +
                    std::to_string(resumed.message_count) + " messages)"
            );
            if ((!resumed.provider.empty() && resumed.provider !=
                    ConfigManager::providerToString(provider)) ||
                (!resumed.model.empty() && resumed.model != model)) {
                ui.showMessage(
                    "Session",
                    "Saved with " + resumed.provider + "/" + resumed.model +
                        "; continuing with " +
                        ConfigManager::providerToString(provider) + "/" + model
                );
            }
        } else {
            ui.showError("Cannot resume session: " + resume_error);
        }
    }
    if (session_id.empty()) {
        session_id = session_mgr.create(
            ConfigManager::providerToString(provider), model
        );
        messages.push_back({
            "system", current_system_prompt, nullptr, "", {}
        });
    }
    auto saveSession = [&]() {
        std::string save_error;
        if (!session_mgr.save(
                session_id,
                ConfigManager::providerToString(provider),
                model,
                messages,
                &save_error
            )) {
            ui.showError("Session autosave failed: " + save_error);
        }
    };
    saveSession();

    bool event_streaming = false;
    auto finishEventStream = [&]() {
        ui.stopThinking();
        if (event_streaming) {
            ui.endStreamingResponse(0.0);
            event_streaming = false;
        }
    };
    AgentSessionCallbacks agent_callbacks;
    agent_callbacks.approve_tool = [&](const ToolApproval& approval) {
        return ui.confirmToolCall(
            approval.action, approval.detail, approval.preview
        );
    };
    agent_callbacks.execute_tool = [&](const std::string& name,
                                       const nlohmann::json& arguments) {
        return tool_registry.executeTool(name, arguments);
    };
    agent_callbacks.persist = saveSession;
    agent_callbacks.emit = [&](const AgentEvent& event) {
        switch (event.type) {
            case AgentEventType::ModelRequestStarted:
                finishEventStream();
                ui.startThinking();
                break;
            case AgentEventType::ContextCompacted:
                ui.showMessage("Context", event.detail);
                break;
            case AgentEventType::ModelTextDelta:
                if (!event_streaming) {
                    ui.stopThinking();
                    ui.beginStreamingResponse();
                    event_streaming = true;
                }
                ui.appendStreamingChunk(event.text);
                break;
            case AgentEventType::ModelResponse:
                finishEventStream();
                ui.showAIResponse(event.text);
                break;
            case AgentEventType::ToolRequested:
                finishEventStream();
                ui.showToolCall(event.tool_name, event.detail);
                break;
            case AgentEventType::ToolDenied:
                ui.showToolResult(
                    event.tool_name, false, "Denied by user"
                );
                break;
            case AgentEventType::ToolCompleted:
                ui.showToolResult(
                    event.tool_name, event.success, event.detail
                );
                break;
            case AgentEventType::UsageUpdated:
                if (event.turn_cost_usd > 0.000001) {
                    std::cout << "   This step: $" << std::fixed
                              << std::setprecision(6)
                              << event.turn_cost_usd << " USD\n";
                }
                std::cout << "   Session total: "
                          << token_tracker.getTotalInputTokens() +
                                 token_tracker.getTotalOutputTokens()
                          << " tokens, $" << std::fixed
                          << std::setprecision(6)
                          << event.total_cost_usd << " USD\n\n";
                break;
            case AgentEventType::TurnFailed:
                finishEventStream();
                ui.showError("Agent error: " + event.text);
                break;
            case AgentEventType::TurnCompleted:
                finishEventStream();
                break;
            default:
                break;
        }
    };
    auto model_provider = createModelProvider(
        ConfigManager::providerToString(provider),
        http_client,
        api_url,
        api_key,
        model
    );
    std::unique_ptr<ModelProvider> subagent_provider;
    if (subagent_enabled) {
        subagent_provider = createModelProvider(
            ConfigManager::providerToString(provider),
            http_client,
            api_url,
            api_key,
            subagent_model
        );
        auto subagent_tools = tool_registry.getToolsSchema();
        subagent_tools.erase(
            std::remove_if(
                subagent_tools.begin(), subagent_tools.end(),
                [](const nlohmann::json& tool) {
                    return tool.value("function", nlohmann::json::object())
                               .value("name", "") == "delegate_task";
                }
            ),
            subagent_tools.end()
        );
        subagent_runner = std::make_unique<SubagentRunner>(
            *subagent_provider,
            std::move(subagent_tools),
            [&](const std::string& name, const nlohmann::json& arguments) {
                return tool_registry.executeTool(name, arguments);
            },
            [&](const ToolApproval& approval) {
                return ui.confirmToolCall(
                    approval.action, approval.detail, approval.preview
                );
            },
            &g_cancellation
        );
    }
    AgentSession agent_session(
        *model_provider,
        messages,
        tool_registry.getToolsSchema(),
        token_tracker,
        std::move(agent_callbacks),
        &g_cancellation
    );
    ui.setTaskPanel(
        "Main conversation", {},
        "Main 0 tok / $0.000000  •  Sub 0 tok / $0.000000"
    );
    ui.enableTaskPanel();
    
    std::cout << "Chat started. Type /help to see available commands.\n\n";
    
    while (!g_interrupted) {
        std::string user_input;
        {
            std::vector<std::string> panel_tasks;
            for (const auto& task : goal_state.subtasks()) {
                panel_tasks.push_back(
                    std::string(task.completed ? "[x] " : "[ ] ") + task.text
                );
            }
            ui.setTaskPanel(
                goal_state.active() ? goal_state.text() : "Main conversation",
                panel_tasks,
                "Main " +
                    std::to_string(
                        token_tracker.getTotalInputTokens() +
                        token_tracker.getTotalOutputTokens()
                    ) +
                    " tok / $" +
                    std::to_string(token_tracker.getTotalCostUSD()) +
                    "  •  Sub " +
                    std::to_string(
                        subagent_runner
                            ? subagent_runner->totalInputTokens() +
                                  subagent_runner->totalOutputTokens()
                            : 0
                    ) +
                    " tok / $" +
                    std::to_string(
                        subagent_runner
                            ? subagent_runner->totalCostUSD()
                            : 0.0
                    )
            );
            user_input = ui.getInput("You");
            
            if (user_input == "/exit" || user_input == "exit" || user_input == "quit") {
                break;
            }
            
            if (user_input == "/stats" || user_input == "stats") {
                ui.showTokenStats(
                    token_tracker.getTotalInputTokens(),
                    token_tracker.getTotalOutputTokens(),
                    token_tracker.getTotalCostUSD()
                );
                if (subagent_runner) {
                    ui.showMessage(
                        "Sub-agent token usage",
                        "Input: " +
                            std::to_string(
                                subagent_runner->totalInputTokens()
                            ) +
                            "\nOutput: " +
                            std::to_string(
                                subagent_runner->totalOutputTokens()
                            ) +
                            "\nEstimated cost: $" +
                            std::to_string(
                                subagent_runner->totalCostUSD()
                            )
                    );
                }
                continue;
            }

            if (user_input == "/help" || user_input == "help") {
                ui.showHelp();
                continue;
            }

            if (user_input == "/diff") {
                std::string diff = git_mgr.getDiff(work_dir);
                if (diff.size() > 200000) {
                    diff.resize(200000);
                    diff += "\n[Diff truncated]";
                }
                ui.showCollapsibleDiff(diff, true);
                continue;
            }

            if (user_input == "/goal" || user_input == "/goal status") {
                if (!goal_state.active()) {
                    ui.showMessage("Goal", "No active goal");
                } else {
                    ui.showMessage("Goal", goal_state.prompt());
                }
                continue;
            }

            if (user_input == "/goal stop") {
                goal_state.complete();
                keep_awake.stop();
                ui.showMessage("Goal", "Goal mode stopped");
                continue;
            }

            if (user_input.rfind("/goal ", 0) == 0) {
                const std::string goal =
                    trimWhitespace(user_input.substr(6));
                if (!goal_state.start(goal)) {
                    ui.showError("Usage: /goal <goal text>");
                } else {
                    keep_awake.start();
                    ui.showMessage(
                        "Goal",
                        "Persistent goal mode enabled; use /goal stop to cancel"
                    );
                    user_input = goal;
                }
                if (user_input.empty() || user_input.front() == '/') continue;
            }

            if (user_input == "/subtask" || user_input == "/subtesk") {
                std::ostringstream listed;
                const auto& tasks = goal_state.subtasks();
                if (tasks.empty()) listed << "No subtasks";
                for (std::size_t i = 0; i < tasks.size(); ++i) {
                    listed << i + 1 << ". [" << (tasks[i].completed ? 'x' : ' ')
                           << "] " << tasks[i].text << '\n';
                }
                ui.showMessage("Subtasks", listed.str());
                continue;
            }

            if (user_input.rfind("/subtask done ", 0) == 0 ||
                user_input.rfind("/subtesk done ", 0) == 0) {
                const auto offset =
                    user_input.rfind("/subtask", 0) == 0 ? 14u : 13u;
                try {
                    const std::size_t index = std::stoull(
                        trimWhitespace(user_input.substr(offset))
                    );
                    if (!goal_state.completeSubtask(index)) {
                        throw std::runtime_error("invalid index");
                    }
                    ui.showMessage("Subtasks", "Marked complete");
                } catch (...) {
                    ui.showError("Usage: /subtask done <number>");
                }
                continue;
            }

            if (user_input.rfind("/subtask ", 0) == 0 ||
                user_input.rfind("/subtesk ", 0) == 0) {
                const auto offset =
                    user_input.rfind("/subtask ", 0) == 0 ? 9u : 8u;
                if (!goal_state.addSubtask(
                        trimWhitespace(user_input.substr(offset)))) {
                    ui.showError(
                        "Start /goal first; subtask text must support the goal"
                    );
                } else {
                    ui.showMessage(
                        "Subtasks",
                        "Added under the active goal; the main goal remains authoritative"
                    );
                }
                continue;
            }

            if (user_input == "/memory") {
                ui.showMessage("Memory", memory_store.display());
                continue;
            }

            if (user_input.rfind("/memory global ", 0) == 0) {
                const bool saved = memory_store.appendGlobal(
                    "User preferences",
                    trimWhitespace(user_input.substr(15))
                );
                ui.showMessage(
                    "Memory", saved ? "Saved to global memory" : "Save failed"
                );
                continue;
            }

            if (user_input.rfind("/memory project ", 0) == 0) {
                const bool saved = memory_store.appendProject(
                    "Project notes",
                    trimWhitespace(user_input.substr(16))
                );
                ui.showMessage(
                    "Memory", saved ? "Saved to project memory" : "Save failed"
                );
                continue;
            }

            if (user_input == "/mcp") {
                ui.showMessage(
                    "MCP",
                    std::to_string(mcp_registry.size()) +
                        " server(s) configured. Usage: /mcp load <config.json>"
                );
                continue;
            }

            if (user_input.rfind("/mcp load ", 0) == 0) {
                const std::string path =
                    trimWhitespace(user_input.substr(10));
                try {
                    nlohmann::json config_json;
                    std::ifstream input(path);
                    if (!input) throw std::runtime_error("cannot open file");
                    input >> config_json;
                    const auto& servers = config_json.contains("mcpServers")
                        ? config_json["mcpServers"] : config_json;
                    if (!servers.is_object()) {
                        throw std::runtime_error(
                            "expected an object or mcpServers object"
                        );
                    }
                    for (const auto& entry : servers.items()) {
                        const auto& spec = entry.value();
                        const std::string command =
                            spec.value("command", "");
                        if (command.empty()) {
                            throw std::runtime_error(
                                "missing command for " + entry.key()
                            );
                        }
                        std::vector<std::string> args;
                        if (spec.contains("args")) {
                            args = spec["args"].get<std::vector<std::string>>();
                        }
                        mcp_registry.addServer(std::make_shared<McpServer>(
                            entry.key(), command, std::move(args)
                        ));
                    }
                    const std::size_t started = mcp_registry.initializeAll();
                    for (const auto& descriptor : mcp_registry.allTools()) {
                        const std::string qualified = descriptor.name;
                        std::string exposed = "mcp__" + qualified;
                        std::replace_if(
                            exposed.begin(), exposed.end(),
                            [](unsigned char ch) {
                                return !std::isalnum(ch) && ch != '_' && ch != '-';
                            },
                            '_'
                        );
                        if (tool_registry.hasTool(exposed)) continue;
                        tool_registry.registerTool({
                            exposed,
                            descriptor.description,
                            [&, qualified](const nlohmann::json& args) {
                                const auto result = mcp_registry.callTool(
                                    qualified, args, &g_cancellation
                                );
                                return nlohmann::json({
                                    {"success", result.success},
                                    {"content", result.content},
                                    {"error", result.error}
                                });
                            },
                            descriptor.parameters
                        });
                    }
                    agent_session.setTools(tool_registry.getToolsSchema());
                    ui.showMessage(
                        "MCP",
                        "Started " + std::to_string(started) +
                            " server(s); model tools refreshed"
                    );
                } catch (const std::exception& error) {
                    ui.showError(
                        std::string("Cannot load MCP config: ") + error.what()
                    );
                }
                continue;
            }

            if (user_input == "/mode" || user_input == "/mode normal") {
                ui.showMessage(
                    "Mode",
                    goal_state.active() ? "goal" : "normal"
                );
                continue;
            }

            if (user_input == "/params") {
                ui.showMessage(
                    "Parameters",
                    "main_model=" + model +
                    "\nsubagent_model=" +
                    (subagent_enabled ? subagent_model : "disabled") +
                    "\nmain_tokens=" +
                    std::to_string(
                        token_tracker.getTotalInputTokens() +
                        token_tracker.getTotalOutputTokens()
                    ) +
                    "\nsubagent_tokens=" +
                    std::to_string(
                        subagent_runner
                            ? subagent_runner->totalInputTokens() +
                                  subagent_runner->totalOutputTokens()
                            : 0
                    ) +
                    "\nmain_cost_usd=" +
                    std::to_string(token_tracker.getTotalCostUSD()) +
                    "\nsubagent_cost_usd=" +
                    std::to_string(
                        subagent_runner
                            ? subagent_runner->totalCostUSD()
                            : 0.0
                    ) +
                    "\ngoal_max_continuations=" +
                    std::to_string(goal_max_continuations)
                );
                continue;
            }

            if (user_input.rfind("/params goal-max ", 0) == 0) {
                try {
                    const std::size_t value = std::stoull(
                        trimWhitespace(user_input.substr(17))
                    );
                    if (value == 0 || value > 256) {
                        throw std::runtime_error("out of range");
                    }
                    goal_max_continuations = value;
                    ui.showMessage(
                        "Parameters",
                        "goal_max_continuations=" + std::to_string(value)
                    );
                } catch (...) {
                    ui.showError("Usage: /params goal-max <1-256>");
                }
                continue;
            }

            if (user_input == "/mode goal") {
                ui.showError("Use /goal <goal text> to enter goal mode");
                continue;
            }

            if (user_input == "/mode review" ||
                user_input == "/code-review") {
                user_input =
                    "Use $review-code and, when configured, delegate an "
                    "independent second-pass review to the sub-agent. Review "
                    "the current working tree and report prioritized findings.";
            } else if (user_input.rfind("/code-review ", 0) == 0) {
                user_input =
                    "Use $review-code and delegate an independent second-pass "
                    "review when possible. Review this scope: " +
                    trimWhitespace(user_input.substr(13));
            }

            if (user_input == "/clear" || user_input == "clear") {
                ui.clearScreen();
                continue;
            }

            if (user_input == "/theme") {
                ui.showAppearanceSettings();
                continue;
            }

            if (user_input == "/skills") {
                const auto listed = skill_mgr.listSkills();
                std::cout << "\n[Skills]\n";
                for (const auto& skill : listed["skills"]) {
                    std::cout << "  $" << skill.value("name", "")
                              << " - " << skill.value("description", "") << '\n';
                }
                continue;
            }

            if (user_input == "/sessions") {
                const auto sessions = session_mgr.list();
                std::cout << "\n[Saved sessions]\n";
                for (const auto& session : sessions) {
                    std::cout << "  " << session.id << "  "
                              << session.title << "  ["
                              << session.message_count << " messages]\n";
                }
                continue;
            }

            if (user_input == "/session") {
                ui.showMessage("Session", session_id + " (autosave enabled)");
                continue;
            }

            if (user_input.rfind("/resume ", 0) == 0) {
                const std::string target =
                    trimWhitespace(user_input.substr(8));
                std::vector<ChatMessage> resumed_messages;
                SessionInfo resumed;
                std::string resume_error;
                if (!session_mgr.load(
                        target, resumed_messages, resumed, &resume_error
                    )) {
                    ui.showError("Cannot resume session: " + resume_error);
                    continue;
                }
                if (!resumed_messages.empty() &&
                    resumed_messages.front().role == "system") {
                    resumed_messages.front().content = current_system_prompt;
                } else {
                    resumed_messages.insert(resumed_messages.begin(), {
                        "system", current_system_prompt, nullptr, "", {}
                    });
                }
                messages = std::move(resumed_messages);
                session_id = target;
                ui.showMessage(
                    "Session",
                    "Resumed " + resumed.title + " (" +
                        std::to_string(resumed.message_count) + " messages)"
                );
                saveSession();
                continue;
            }

            if (user_input == "/changes") {
                const auto listed = change_journal.list();
                std::cout << "\n[Reversible changes]\n";
                for (const auto& change : listed["changes"]) {
                    std::cout << "  #" << change.value("id", 0)
                              << (change.value("undone", false) ? " [undone] " : " ")
                              << change.value("action", "") << "  "
                              << change.value("target", "") << '\n';
                }
                continue;
            }

            if (user_input == "/undo" || user_input.rfind("/undo ", 0) == 0) {
                std::uint64_t id = 0;
                if (user_input.size() > 5) {
                    try {
                        id = std::stoull(trimWhitespace(user_input.substr(6)));
                    } catch (...) {
                        ui.showError("Usage: /undo or /undo <change-id>");
                        continue;
                    }
                }
                const auto result = change_journal.undo(id);
                if (result.value("success", false)) {
                    skill_mgr.discover(skill_roots);
                    ui.showMessage(
                        "Undo",
                        "Restored change #" +
                            std::to_string(result.value("change_id", 0))
                    );
                } else {
                    ui.showError(result.value("error", "Undo failed"));
                }
                continue;
            }

            if (user_input.rfind("/theme ", 0) == 0) {
                std::string theme = user_input.substr(7);
                std::transform(theme.begin(), theme.end(), theme.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (ui.setTheme(theme)) {
                    ui.showMessage("Settings", "Theme changed to " + theme);
                } else {
                    ui.showError("Unknown theme. Use /theme to list available themes.");
                }
                continue;
            }

            if (user_input == "/animation on") {
                if (ui.setAnimations(true)) {
                    ui.showMessage("Settings", "Thinking animation enabled");
                } else {
                    ui.showError("Animation requires an interactive terminal.");
                }
                continue;
            }

            if (user_input == "/animation off") {
                ui.setAnimations(false);
                ui.showMessage("Settings", "Thinking animation disabled");
                continue;
            }

            if (user_input == "/mouse on") {
                if (ui.setMouseLinks(true)) {
                    ui.showMessage("Settings", "Clickable file links enabled");
                } else {
                    ui.showError("Mouse links require an interactive terminal.");
                }
                continue;
            }

            if (user_input == "/mouse off") {
                ui.setMouseLinks(false);
                ui.showMessage("Settings", "Clickable file links disabled");
                continue;
            }

            if (!user_input.empty() && user_input.front() == '/') {
                ui.showError("Unknown command. Type /help for available commands.");
                continue;
            }
            
            if (user_input.empty()) {
                continue;
            }
            
            g_cancellation.reset();
            g_operation_active = 1;
            if (ui.selectedTaskIndex() > 0 &&
                ui.selectedTaskIndex() <= goal_state.subtasks().size()) {
                const auto& selected =
                    goal_state.subtasks()[ui.selectedTaskIndex() - 1];
                user_input =
                    "Work within this selected subtask while preserving the "
                    "main goal: " + selected.text + "\n\nUser input: " +
                    user_input;
            }
            if (!messages.empty() && messages.front().role == "system") {
                current_system_prompt = currentSystemPrompt();
                messages.front().content = current_system_prompt;
            }
            AgentRunResult run_result = agent_session.runTurn(user_input);
            std::size_t goal_steps = 0;
            while (goal_state.active() && run_result.success &&
                   run_result.final_response.find("[GOAL_COMPLETE]") ==
                       std::string::npos &&
                   !g_cancellation.requested() && !g_interrupted &&
                   goal_steps++ < goal_max_continuations) {
                ui.showMessage(
                    "Goal progress",
                    "continuation " + std::to_string(goal_steps) + "/" +
                        std::to_string(goal_max_continuations) +
                        " | subtasks " +
                        std::to_string(goal_state.completedSubtasks()) + "/" +
                        std::to_string(goal_state.subtasks().size()) +
                        " | main tokens " +
                        std::to_string(
                            token_tracker.getTotalInputTokens() +
                            token_tracker.getTotalOutputTokens()
                        ) +
                        " | sub-agent tokens " +
                        std::to_string(
                            subagent_runner
                                ? subagent_runner->totalInputTokens() +
                                      subagent_runner->totalOutputTokens()
                                : 0
                        )
                );
                current_system_prompt = currentSystemPrompt();
                if (!messages.empty() && messages.front().role == "system") {
                    messages.front().content = current_system_prompt;
                }
                run_result = agent_session.runTurn(
                    "Continue working toward the active goal. Verify actual "
                    "progress and do not claim completion prematurely."
                );
            }
            if (goal_state.active() && run_result.success &&
                run_result.final_response.find("[GOAL_COMPLETE]") !=
                    std::string::npos) {
                memory_store.appendProject(
                    "Completed goals", goal_state.text()
                );
                goal_state.complete();
                keep_awake.stop();
                ui.showMessage("Goal", "Goal completed and recorded");
            }
            g_operation_active = 0;
            if (g_cancellation.requested() && !g_interrupted) {
                ui.showMessage(
                    "Cancelled",
                    "Current operation stopped. Press Ctrl-C again while busy "
                    "to exit, or continue with another prompt."
                );
            }
            continue;
        }
    }

    
    // 最终统计
    saveSession();
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->role != "assistant" || it->content.empty()) continue;
        std::string summary = it->content;
        if (summary.size() > 2000) {
            summary.resize(2000);
            summary += "...";
        }
        memory_store.appendProject(
            "Conversation summaries",
            "Session " + session_id + ": " + summary
        );
        break;
    }
    std::cout << "\n[Session Summary]\n"
              << "  Total input tokens:  " << token_tracker.getTotalInputTokens() << '\n'
              << "  Total output tokens: " << token_tracker.getTotalOutputTokens() << '\n'
              << "  Estimated cost:      $" << std::fixed << std::setprecision(6)
              << token_tracker.getTotalCostUSD() << " USD\n";
    if (subagent_runner) {
        std::cout << "  Sub-agent input:   "
                  << subagent_runner->totalInputTokens() << '\n'
                  << "  Sub-agent output:  "
                  << subagent_runner->totalOutputTokens() << '\n'
                  << "  Sub-agent cost:    $" << std::fixed
                  << std::setprecision(6)
                  << subagent_runner->totalCostUSD() << " USD\n";
    }
    
    std::cout << "\nGoodbye.\n";
    
    return 0;
}
