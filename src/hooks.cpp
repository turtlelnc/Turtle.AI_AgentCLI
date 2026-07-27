#include "hooks.hpp"

#include <algorithm>

namespace opencode {

void HookRegistry::registerHook(Hook hook) {
    hooks_.push_back(std::move(hook));
}

bool HookRegistry::unregisterHook(const std::string& name) {
    auto it = std::find_if(hooks_.begin(), hooks_.end(),
        [&](const Hook& h) { return h.name == name; });
    if (it == hooks_.end()) return false;
    hooks_.erase(it);
    return true;
}

bool HookRegistry::run(HookEvent event, const nlohmann::json& context) {
    for (const auto& hook : hooks_) {
        if (hook.event != event) continue;
        try {
            HookResult result = hook.handler(context);
            if (!result.allow) return false;
        } catch (...) {
            // Hook exceptions do not crash the host.
            continue;
        }
    }
    return true;
}

std::vector<std::string> HookRegistry::list() const {
    std::vector<std::string> names;
    for (const auto& h : hooks_) names.push_back(h.name);
    return names;
}

void HookRegistry::clear() {
    hooks_.clear();
}

} // namespace opencode
