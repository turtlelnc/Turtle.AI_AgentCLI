#include "secret_store.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

#if defined(__APPLE__)
#include <Security/Security.h>
#endif

namespace opencode {

namespace {

std::string normalizedProvider(std::string provider) {
    std::transform(provider.begin(), provider.end(), provider.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return provider;
}

const char* providerEnvironmentVariable(const std::string& provider) {
    const std::string normalized = normalizedProvider(provider);
    if (normalized == "openai") return "OPENAI_API_KEY";
    if (normalized == "anthropic") return "ANTHROPIC_API_KEY";
    if (normalized == "deepseek") return "DEEPSEEK_API_KEY";
    return nullptr;
}

#if defined(__APPLE__)
CFStringRef makeString(const std::string& value) {
    return CFStringCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(value.data()),
        static_cast<CFIndex>(value.size()),
        kCFStringEncodingUTF8,
        false
    );
}

CFMutableDictionaryRef keychainQuery(
    const std::string& service,
    const std::string& account
) {
    auto query = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );
    CFStringRef service_value = makeString(service);
    CFStringRef account_value = makeString(account);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service_value);
    CFDictionarySetValue(query, kSecAttrAccount, account_value);
    CFRelease(service_value);
    CFRelease(account_value);
    return query;
}

std::string keychainError(OSStatus status) {
    CFStringRef message = SecCopyErrorMessageString(status, nullptr);
    if (!message) return "macOS Keychain operation failed";
    std::vector<char> buffer(
        static_cast<std::size_t>(
            CFStringGetMaximumSizeForEncoding(
                CFStringGetLength(message), kCFStringEncodingUTF8
            )
        ) + 1
    );
    const bool copied = CFStringGetCString(
        message, buffer.data(), static_cast<CFIndex>(buffer.size()),
        kCFStringEncodingUTF8
    );
    CFRelease(message);
    return copied ? std::string(buffer.data())
                  : "macOS Keychain operation failed";
}
#endif

class PlatformSecretStore final : public SecretStore {
public:
    std::optional<SecretValue> get(const std::string& provider) const override {
        if (const char* variable = providerEnvironmentVariable(provider)) {
            if (const char* value = std::getenv(variable); value && *value) {
                return SecretValue{value, variable};
            }
        }
        // Backward-compatible read only. Turtle never writes this variable.
        if (const char* legacy = std::getenv("OPENCODE_API_KEY");
            legacy && *legacy) {
            return SecretValue{legacy, "OPENCODE_API_KEY (legacy)"};
        }
#if defined(__APPLE__)
        const std::string account = normalizedProvider(provider);
        CFMutableDictionaryRef query = keychainQuery(service_, account);
        CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
        CFTypeRef result = nullptr;
        const OSStatus status = SecItemCopyMatching(query, &result);
        CFRelease(query);
        if (status == errSecSuccess && result &&
            CFGetTypeID(result) == CFDataGetTypeID()) {
            const auto data = static_cast<CFDataRef>(result);
            std::string value(
                reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
                static_cast<std::size_t>(CFDataGetLength(data))
            );
            CFRelease(result);
            return SecretValue{std::move(value), "macOS Keychain"};
        }
        if (result) CFRelease(result);
#endif
        return std::nullopt;
    }

    bool store(
        const std::string& provider,
        const std::string& secret,
        std::string* error
    ) override {
#if defined(__APPLE__)
        if (secret.empty()) {
            if (error) *error = "Refusing to store an empty credential";
            return false;
        }
        const std::string account = normalizedProvider(provider);
        CFDataRef secret_data = CFDataCreate(
            kCFAllocatorDefault,
            reinterpret_cast<const UInt8*>(secret.data()),
            static_cast<CFIndex>(secret.size())
        );
        CFMutableDictionaryRef query = keychainQuery(service_, account);
        auto updates = CFDictionaryCreateMutable(
            kCFAllocatorDefault, 0,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks
        );
        CFDictionarySetValue(updates, kSecValueData, secret_data);
        OSStatus status = SecItemUpdate(query, updates);
        CFRelease(updates);
        if (status == errSecItemNotFound) {
            CFDictionarySetValue(query, kSecValueData, secret_data);
            status = SecItemAdd(query, nullptr);
        }
        CFRelease(query);
        CFRelease(secret_data);
        if (status != errSecSuccess) {
            if (error) {
                *error = keychainError(status);
            }
            return false;
        }
        return true;
#else
        (void)provider;
        (void)secret;
        if (error) {
            *error = "No secure persistent credential store is available";
        }
        return false;
#endif
    }

    bool supportsPersistentStore() const override {
#if defined(__APPLE__)
        return true;
#else
        return false;
#endif
    }

private:
    const std::string service_ = "Turtle.AI AgentCLI";
};

} // namespace

std::unique_ptr<SecretStore> createSecretStore() {
    return std::make_unique<PlatformSecretStore>();
}

} // namespace opencode
