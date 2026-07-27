#ifndef ERROR_CATEGORY_HPP
#define ERROR_CATEGORY_HPP

#include <string>

namespace opencode {

enum class ErrorCategory {
    None,
    Authentication,
    RateLimited,
    ContextOverflow,
    Transport,
    Provider,
    InvalidToolCall,
    ToolExecution,
    Cancelled
};

const char* errorCategoryName(ErrorCategory category);
ErrorCategory errorCategoryFromName(const std::string& name);

} // namespace opencode

#endif
