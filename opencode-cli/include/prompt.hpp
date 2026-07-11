#ifndef PROMPT_HPP
#define PROMPT_HPP

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace opencode {

struct Message {
    std::string role;  // "system", "user", "assistant"
    std::string content;
};

class PromptManager {
public:
    PromptManager();
    
    void set_system_prompt(const std::string& prompt);
    void add_message(const std::string& role, const std::string& content);
    std::vector<Message> get_messages() const;
    nlohmann::json to_openai_format() const;
    nlohmann::json to_anthropic_format() const;
    nlohmann::json to_llamacpp_format() const;
    
    void clear();
    
private:
    std::string system_prompt_;
    std::vector<Message> messages_;
};

// Default system prompts
std::string get_default_system_prompt();

} // namespace opencode

#endif // PROMPT_HPP
