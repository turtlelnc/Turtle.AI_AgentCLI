#ifndef TOKEN_TRACKER_HPP
#define TOKEN_TRACKER_HPP

#include <string>
#include <unordered_map>

namespace opencode {

struct ModelPricing {
    double input_price_per_1m;   // 每百万输入 token 价格 (USD)
    double output_price_per_1m;  // 每百万输出 token 价格 (USD)
};

class TokenTracker {
public:
    TokenTracker();
    
    // 设置当前模型
    void setModel(const std::string& model);
    
    // 记录 token 使用
    void recordTokens(int64_t input_tokens, int64_t output_tokens);
    
    // 获取当前会话统计
    int64_t getTotalInputTokens() const;
    int64_t getTotalOutputTokens() const;
    double getTotalCostUSD() const;
    
    // 重置统计
    void reset();
    
    // 获取模型价格信息
    static ModelPricing getModelPrice(const std::string& model);
    
private:
    std::string current_model_;
    int64_t total_input_tokens_;
    int64_t total_output_tokens_;
    double total_cost_usd_;
    
    // 内置主流模型价格表 (基于公开文档，2024 年数据)
    static const std::unordered_map<std::string, ModelPricing> model_prices_;
};

} // namespace opencode

#endif // TOKEN_TRACKER_HPP
