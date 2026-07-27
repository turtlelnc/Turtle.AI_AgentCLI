#include "config_manager.hpp"
#include "secret_store.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

std::string read(const std::filesystem::path& path) {
    std::ifstream input(path);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const std::string secret = "test-secret-must-not-be-persisted";
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    const fs::path root = fs::temp_directory_path() / ("turtle-config-" + unique);
    fs::create_directories(root);
    const fs::path legacy = root / "legacy.json";
    {
        std::ofstream output(legacy);
        output << R"({"provider":"OpenAI","api_url":"https://api.openai.com/v1/chat/completions",)"
               << R"("api_key":")" << secret << R"(","model":"test","work_dir":".","use_git":false})";
    }

    opencode::ConfigManager manager;
    bool passed = expect(manager.loadConfig(legacy.string()), "legacy config loads");
    passed &= expect(
        read(legacy).find(secret) != std::string::npos,
        "legacy credential remains until migration is confirmed"
    );
    const auto migrated =
        manager.takeLegacyApiKey(opencode::ProviderType::OpenAI);
    passed &= expect(
        migrated && *migrated == secret,
        "legacy credential remains available only in process memory"
    );
    passed &= expect(
        manager.sanitizeLegacyCredentials() &&
            read(legacy).find(secret) == std::string::npos &&
            read(legacy).find("api_key") == std::string::npos,
        "confirmed migration removes the legacy credential"
    );

    manager.getConfig().api_key = secret;
    const fs::path current = root / ".turtle" / "config.json";
    passed &= expect(manager.saveConfig(current.string()), "new config saves");
    passed &= expect(
        read(current).find(secret) == std::string::npos &&
            read(current).find("api_key") == std::string::npos,
        "new config never serializes credentials"
    );

#if defined(_WIN32)
    _putenv_s("OPENAI_API_KEY", secret.c_str());
#else
    setenv("OPENAI_API_KEY", secret.c_str(), 1);
#endif
    auto store = opencode::createSecretStore();
    const auto from_environment = store->get("OpenAI");
    passed &= expect(
        from_environment && from_environment->value == secret &&
            from_environment->source == "OPENAI_API_KEY",
        "standard provider environment variable is supported"
    );
#if defined(_WIN32)
    _putenv_s("OPENAI_API_KEY", "");
#else
    unsetenv("OPENAI_API_KEY");
#endif

    const fs::path migration_home = root / "home";
    fs::create_directories(migration_home);
    {
        std::ofstream output(migration_home / ".opencode_config.json");
        output << R"({"provider":"DeepSeek","api_key":"config-old-secret",)"
               << R"("api_url":"https://api.deepseek.com/v1/chat/completions","model":"old","work_dir":".","use_git":false})";
    }
    {
        std::ofstream output(migration_home / ".opencode_env");
        output << "KEEP_THIS=value\nOPENCODE_API_KEY=dotenv-old-secret\n";
    }
#if defined(_WIN32)
    const char* original_home_value = std::getenv("USERPROFILE");
    if (!original_home_value) original_home_value = std::getenv("HOME");
    const std::string original_home =
        original_home_value ? original_home_value : "";
    _putenv_s("HOME", migration_home.string().c_str());
#else
    const char* original_home_value = std::getenv("HOME");
    const std::string original_home =
        original_home_value ? original_home_value : "";
    setenv("HOME", migration_home.string().c_str(), 1);
#endif
    opencode::ConfigManager migration_manager;
    passed &= expect(
        migration_manager.loadDefaultConfig(),
        "legacy Turtle predecessor config migrates"
    );
    passed &= expect(
        migration_manager.sanitizeLegacyCredentials(),
        "detected legacy credential files can be sanitized"
    );
    passed &= expect(
        read(migration_home / ".opencode_config.json").find("config-old-secret") ==
            std::string::npos &&
        read(migration_home / ".opencode_env").find("dotenv-old-secret") ==
            std::string::npos &&
        read(migration_home / ".turtle/config.json").find("api_key") ==
            std::string::npos,
        "legacy JSON and dotenv credentials are removed from disk"
    );
#if defined(_WIN32)
    if (original_home.empty()) _putenv_s("HOME", "");
    else _putenv_s("HOME", original_home.c_str());
#else
    if (original_home.empty()) unsetenv("HOME");
    else setenv("HOME", original_home.c_str(), 1);
#endif

    std::error_code error;
    fs::remove_all(root, error);
    return passed ? 0 : 1;
}
