#include "prompt.hpp"

namespace opencode {

PromptManager::PromptManager() 
    : system_prompt_(get_default_system_prompt()) {}

void PromptManager::set_system_prompt(const std::string& prompt) {
    system_prompt_ = prompt;
}

void PromptManager::add_message(const std::string& role, const std::string& content) {
    messages_.push_back({role, content});
}

std::vector<Message> PromptManager::get_messages() const {
    return messages_;
}

nlohmann::json PromptManager::to_openai_format() const {
    nlohmann::json messages = nlohmann::json::array();
    
    // Add system prompt
    if (!system_prompt_.empty()) {
        messages.push_back({{"role", "system"}, {"content", system_prompt_}});
    }
    
    // Add conversation messages
    for (const auto& msg : messages_) {
        messages.push_back({{"role", msg.role}, {"content", msg.content}});
    }
    
    return messages;
}

nlohmann::json PromptManager::to_anthropic_format() const {
    // Anthropic uses separate system field and messages array
    nlohmann::json messages = nlohmann::json::array();
    
    for (const auto& msg : messages_) {
        messages.push_back({{"role", msg.role}, {"content", msg.content}});
    }
    
    return messages;
}

nlohmann::json PromptManager::to_llamacpp_format() const {
    // Llama.cpp uses similar format to OpenAI
    return to_openai_format();
}

void PromptManager::clear() {
    messages_.clear();
}

std::string get_default_system_prompt() {
    return R"(You are OpenCode, an AI-powered coding assistant integrated into a C++ CLI tool.

Your capabilities include:
- Writing, reviewing, and debugging code
- Answering technical questions about programming
- Helping with software architecture and design patterns
- Assisting with Git operations and version control
- Using MCP tools to read files, list directories, and search for files

Guidelines:
- Provide clear, concise, and accurate responses
- Include code examples when relevant
- Explain your reasoning for complex solutions
- Be aware of the current working directory context
- Use available MCP tools when you need to inspect files or directories

You are helpful, harmless, and honest in all interactions.)";
}

} // namespace opencode
