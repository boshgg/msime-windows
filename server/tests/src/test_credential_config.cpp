#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "config/credential_store.h"
#include "config/ime_config.h"
#include "tests/includes/test_framework.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace
{
std::string Read(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

void WriteFixture(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    file.close();
    REQUIRE(static_cast<bool>(file));
}

struct ScopedEnv
{
    std::wstring key, previous;
    bool existed = false;
    ScopedEnv(const wchar_t *name, const std::filesystem::path &value) : key(name)
    {
        const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
        existed = length > 0;
        if (existed)
        {
            previous.resize(length);
            previous.resize(GetEnvironmentVariableW(name, previous.data(), length));
        }
        REQUIRE(SetEnvironmentVariableW(name, value.c_str()) != FALSE);
    }
    ~ScopedEnv()
    {
        SetEnvironmentVariableW(key.c_str(), existed ? previous.c_str() : nullptr);
    }
};

struct ConfigFixture
{
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        (L"msime-凭证测试-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    ScopedEnv config_dir{L"METASEQUOIA_IME_CONFIG_DIR", root};
    ScopedEnv app_data{L"LOCALAPPDATA", root};
    const std::string defaults = "[ai_assistant]\nprovider = 'glm'\nmodel = 'glm-5.3'\ntoken = ''\ntoken_glm = ''\n"
                                 "[glm_translation]\napi_key = ''\n[voice_input]\nasr_token = ''\n"
                                 "[tencent_tmt]\nsecret_id = ''\nsecret_key = ''\n";
    ConfigFixture()
    {
        std::filesystem::create_directories(root);
        WriteFixture(root / L"config.default.toml", defaults);
    }
    ~ConfigFixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    void RequireNoPlaintext(const std::string &secret)
    {
        for (const auto &entry : std::filesystem::directory_iterator(root))
            if (entry.is_regular_file())
                REQUIRE(Read(entry.path()).find(secret) == std::string::npos);
    }
};
} // namespace

TEST_CASE(credential_config_migrates_plaintext_before_template_upgrade_and_load)
{
    ConfigFixture fixture;
    const std::string secret = "fixture-legacy-glm-key";
    WriteFixture(fixture.root / L"config.toml",
                 "[ai_assistant]\nprovider = 'glm'\ntoken = '" + secret + "'\ntoken_glm = '" + secret + "'\n");
    InitImeConfig();
    REQUIRE_EQ(GetConfiguredAiAssistant().token, secret);
    fixture.RequireNoPlaintext(secret);
    const std::string first = Read(fixture.root / L"config.toml");
    InitImeConfig();
    REQUIRE_EQ(GetConfiguredAiAssistant().token, secret);
    REQUIRE_EQ(Read(fixture.root / L"config.toml"), first);
    REQUIRE(!std::filesystem::exists(fixture.root / L"config.toml.tmp"));
}

TEST_CASE(credential_config_encrypts_new_ai_translation_and_voice_settings)
{
    ConfigFixture fixture;
    InitImeConfig();
    const std::string secret = "fixture-new-api-key";
    REQUIRE(SetConfiguredAiAssistantString("token", secret));
    REQUIRE(SetConfiguredGlmTranslationString("api_key", secret));
    REQUIRE(SetConfiguredTencentTmtString("secret_key", secret));
    REQUIRE(SetConfiguredVoiceInputString("asr_token", secret));
    fixture.RequireNoPlaintext(secret);
    InitImeConfig();
    REQUIRE_EQ(GetConfiguredAiAssistant().token, secret);
    REQUIRE_EQ(GetConfiguredGlmTranslation().api_key, secret);
    REQUIRE_EQ(GetConfiguredTencentTmt().secret_key, secret);
    REQUIRE_EQ(GetConfiguredVoiceInput().asr_token, secret);
}

TEST_CASE(credential_config_migrates_on_load_without_an_installed_template)
{
    ConfigFixture fixture;
    std::filesystem::remove(fixture.root / L"config.default.toml");
    const std::string secret = "fixture-no-template-key";
    WriteFixture(fixture.root / L"config.toml", "[ai_assistant]\nprovider = 'glm'\ntoken_glm = '" + secret + "'\n");
    InitImeConfig();
    REQUIRE_EQ(GetConfiguredAiAssistant().token, secret);
    fixture.RequireNoPlaintext(secret);
}

TEST_CASE(credential_config_failed_atomic_replace_keeps_the_old_file)
{
    ConfigFixture fixture;
    InitImeConfig();
    const auto path = fixture.root / L"config.toml";
    const std::string original = Read(path);
    // Deny delete sharing while allowing the serializer to read and prepare its
    // encrypted temporary file. Renaming must fail without a truncate fallback.
    HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(locked != INVALID_HANDLE_VALUE);
    const bool saved = SetConfiguredAiAssistantString("token", "fixture-failed-save-key");
    CloseHandle(locked);
    REQUIRE(!saved);
    REQUIRE_EQ(Read(path), original);
    fixture.RequireNoPlaintext("fixture-failed-save-key");
    REQUIRE(!std::filesystem::exists(fixture.root / L"config.toml.tmp"));
}

TEST_CASE(credential_config_refuses_to_overwrite_unreadable_ciphertext)
{
    ConfigFixture fixture;
    const std::string bad = "[ai_assistant]\nprovider = 'glm'\ntoken_glm = 'dpapi:v1:00'\n";
    WriteFixture(fixture.root / L"config.toml", bad);
    InitImeConfig();
    REQUIRE_EQ(Read(fixture.root / L"config.toml"), bad);
    REQUIRE(!SetConfiguredAiAssistantString("token", "replacement-secret"));
    REQUIRE(!SetConfiguredAiAssistantString("model", "glm-5.3"));
    REQUIRE_EQ(Read(fixture.root / L"config.toml"), bad);
}

TEST_CASE(credential_config_recovers_after_explicitly_clearing_inaccessible_values)
{
    ConfigFixture fixture;
    std::string preserved;
    REQUIRE(ConfigCredentials::Encrypt("fixture-unrelated-key", preserved));
    const std::string unrelated = "[tencent_tmt]\nsecret_key = '" + preserved + "'\n";
    const auto path = fixture.root / L"config.toml";
    WriteFixture(path, "[ai_assistant]\nprovider = 'glm'\ntoken_glm = 'dpapi:v1:00'\n" + unrelated);
    InitImeConfig();
    REQUIRE(!SetConfiguredAiAssistantString("token", "fixture-reentered-key"));
    // This simulates the documented manual recovery action after keeping an
    // encrypted backup; the application itself must never clear an opaque value.
    WriteFixture(path, "[ai_assistant]\nprovider = 'glm'\ntoken_glm = ''\n" + unrelated);
    InitImeConfig();
    REQUIRE(SetConfiguredAiAssistantString("token", "fixture-reentered-key"));
    InitImeConfig();
    REQUIRE_EQ(GetConfiguredAiAssistant().token, std::string("fixture-reentered-key"));
    REQUIRE_EQ(GetConfiguredTencentTmt().secret_key, std::string("fixture-unrelated-key"));
    fixture.RequireNoPlaintext("fixture-reentered-key");
    fixture.RequireNoPlaintext("fixture-unrelated-key");
}

TEST_CASE(credential_config_corrupt_recovery_never_creates_plaintext_backups)
{
    ConfigFixture fixture;
    const std::string secret = "fixture-corrupt-key";
    const std::string corrupt = "[ai_assistant]\nprovider = 'glm'\ntoken_glm = '" + secret + "'\nbroken = \n";
    WriteFixture(fixture.root / L"config.toml", corrupt);
    InitImeConfig();
    REQUIRE_EQ(GetConfiguredAiAssistant().token, secret);
    fixture.RequireNoPlaintext(secret);
    bool found_backup = false;
    for (const auto &entry : std::filesystem::directory_iterator(fixture.root))
    {
        if (entry.path().extension() != L".dpapi")
            continue;
        std::string recovered;
        REQUIRE(ConfigCredentials::Decrypt(Read(entry.path()), recovered));
        REQUIRE_EQ(recovered, corrupt);
        found_backup = true;
    }
    REQUIRE(found_backup);
}

TEST_CASE(credential_config_corrupt_recovery_preserves_unreadable_encrypted_values)
{
    ConfigFixture fixture;
    const std::string bad = "[ai_assistant]\ntoken_glm = 'dpapi:v1:00'\nbroken = \n";
    WriteFixture(fixture.root / L"config.toml", bad);
    InitImeConfig();
    REQUIRE_EQ(Read(fixture.root / L"config.toml"), bad);
    REQUIRE(!SetConfiguredAiAssistantString("token", "replacement-secret"));
    REQUIRE_EQ(Read(fixture.root / L"config.toml"), bad);
}
