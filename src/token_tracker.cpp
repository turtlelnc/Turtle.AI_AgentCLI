#include "config_manager.hpp"
#include "token_tracker.hpp"
#include <fstream>
#include <iostream>

namespace opencode {

// 内置模型价格表 (基于公开文档，2024 年数据)
const std::unordered_map<std::string, ModelPricing> TokenTracker::model_prices_ = {
    // DeepSeek 模型
    {"deepseek-chat", {0.14, 0.28}},
    {"deepseek-coder", {0.14, 0.28}},
    {"deepseek-v3", {0.27, 1.10}},
    
    // OpenAI 模型
    {"gpt-4o", {2.50, 10.00}},
    {"gpt-4o-mini", {0.15, 0.60}},
    {"gpt-4-turbo", {10.00, 30.00}},
    {"gpt-3.5-turbo", {0.50, 1.50}},
    
    // Anthropic 模型
    {"claude-3-5-sonnet-20241022", {3.00, 15.00}},
    {"claude-3-opus-20240229", {15.00, 75.00}},
    {"claude-3-haiku-20240307", {0.25, 1.25}},
    
    // Google 模型
    {"gemini-1.5-pro", {1.25, 5.00}},
    {"gemini-1.5-flash", {0.075, 0.30}},
    
    // Groq 模型
    {"llama-3.1-70b-versatile", {0.59, 0.79}},
    {"llama-3.1-8b-instant", {0.05, 0.08}},
    
    // Mistral 模型
    {"mistral-large-latest", {2.00, 6.00}},
    {"mistral-small-latest", {0.20, 0.60}},
    
    // Cohere 模型
    {"command-r-plus", {2.50, 10.00}},
    {"command-r", {0.15, 0.60}},
    
    // TogetherAI 模型
    {"meta-llama/Llama-3-70b-chat-hf", {0.90, 0.90}},
    
    // Ollama / LlamaCpp (本地运行，无 API 费用)
    {"ollama/llama3", {0.0, 0.0}},
    {"llama.cpp/llama3", {0.0, 0.0}},
};

TokenTracker::TokenTracker() 
    : current_model_(""), 
      total_input_tokens_(0), 
      total_output_tokens_(0), 
      total_cost_usd_(0.0) {}

void TokenTracker::setModel(const std::string& model) {
    current_model_ = model;
}

void TokenTracker::recordTokens(int64_t input_tokens, int64_t output_tokens) {
    total_input_tokens_ += input_tokens;
    total_output_tokens_ += output_tokens;
    
    // 计算费用
    ModelPricing pricing = getModelPrice(current_model_);
    double input_cost = (static_cast<double>(input_tokens) / 1000000.0) * pricing.input_price_per_1m;
    double output_cost = (static_cast<double>(output_tokens) / 1000000.0) * pricing.output_price_per_1m;
    total_cost_usd_ += input_cost + output_cost;
}

int64_t TokenTracker::getTotalInputTokens() const {
    return total_input_tokens_;
}

int64_t TokenTracker::getTotalOutputTokens() const {
    return total_output_tokens_;
}

double TokenTracker::getTotalCostUSD() const {
    return total_cost_usd_;
}

void TokenTracker::reset() {
    total_input_tokens_ = 0;
    total_output_tokens_ = 0;
    total_cost_usd_ = 0.0;
}

ModelPricing TokenTracker::getModelPrice(const std::string& model) {
    auto it = model_prices_.find(model);
    if (it != model_prices_.end()) {
        return it->second;
    }
    
    // 尝试模糊匹配 (处理带版本号的情况)
    for (const auto& [key, price] : model_prices_) {
        if (model.find(key) != std::string::npos || key.find(model) != std::string::npos) {
            return price;
        }
    }
    
    // 未知模型，返回默认值 (按 GPT-3.5 计费)
    return {0.50, 1.50};
}

} // namespace opencode
