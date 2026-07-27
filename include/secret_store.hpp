#ifndef SECRET_STORE_HPP
#define SECRET_STORE_HPP

#include <memory>
#include <optional>
#include <string>

namespace opencode {

struct SecretValue {
    std::string value;
    std::string source;
};

class SecretStore {
public:
    virtual ~SecretStore() = default;
    virtual std::optional<SecretValue> get(
        const std::string& provider
    ) const = 0;
    virtual bool store(
        const std::string& provider,
        const std::string& secret,
        std::string* error = nullptr
    ) = 0;
    virtual bool supportsPersistentStore() const = 0;
};

std::unique_ptr<SecretStore> createSecretStore();

} // namespace opencode

#endif
