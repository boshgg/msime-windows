#include "config/credential_store.h"
#include "tests/includes/test_framework.h"
#include <toml++/toml.h>
#include <string>

TEST_CASE(credential_dpapi_round_trip_and_randomized_ciphertext)
{
    const std::string secret = "test-key-中文-\\\"\n";
    std::string first, second, decoded;
    REQUIRE(ConfigCredentials::Encrypt(secret, first));
    REQUIRE(ConfigCredentials::Encrypt(secret, second));
    REQUIRE(ConfigCredentials::IsEncrypted(first));
    REQUIRE(first.find(secret) == std::string::npos);
    REQUIRE(first != second);
    REQUIRE(ConfigCredentials::Decrypt(first, decoded));
    REQUIRE_EQ(decoded, secret);
    REQUIRE(ConfigCredentials::Decrypt(second, decoded));
    REQUIRE_EQ(decoded, secret);
}

TEST_CASE(credential_dpapi_rejects_corruption_and_unknown_versions_without_output)
{
    std::string encoded;
    REQUIRE(ConfigCredentials::Encrypt("test-secret", encoded));
    encoded.back() = encoded.back() == '0' ? '1' : '0';
    for (const std::string &bad : {encoded, std::string("dpapi:v1:00"), std::string("dpapi:v1:xyz"),
                                   std::string("dpapi:v2:00"), std::string("plaintext")})
    {
        std::string output = "unchanged";
        REQUIRE(!ConfigCredentials::Decrypt(bad, output));
        REQUIRE_EQ(output, std::string("unchanged"));
    }
}

TEST_CASE(credential_toml_covers_ai_translation_and_voice_slots_preserving_format)
{
    const std::string original = "# keep this comment\r\n[ai_assistant]\r\n"
                                 "token = 'legacy-ai' # inline comment\r\ntoken_glm = \"glm-ai\"\r\n"
                                 "token_future_provider = \"future-ai\"\r\nmodel = \"glm-5.3\"\r\n"
                                 "[glm_translation]\r\napi_key = \"glm-translation\"\r\n"
                                 "[tencent_tmt]\r\nsecret_id = \"tencent-id\"\r\nsecret_key = \"tencent-key\"\r\n"
                                 "[niutrans]\r\napp_id = \"niutrans-id\"\r\napikey = \"niutrans-key\"\r\n"
                                 "[voice_input]\r\nasr_app_key = \"voice-app\"\r\nasr_token = \"voice-default\"\r\n"
                                 "asr_token_doubao = \"voice-provider\"\r\npolish_token = \"polish-default\"\r\n"
                                 "polish_token_deepseek = \"polish-provider\"\r\n"
                                 "[custom_translation]\r\napi_key = \"custom-key\"\r\n";
    std::string protected_text = original;
    REQUIRE(ConfigCredentials::ProtectToml(protected_text));
    REQUIRE(protected_text.find("# keep this comment\r\n") == 0);
    REQUIRE(protected_text.find(" # inline comment\r\n") != std::string::npos);
    REQUIRE(protected_text.find("model = \"glm-5.3\"") != std::string::npos);
    for (const char *secret : {"legacy-ai", "glm-ai", "future-ai", "glm-translation", "tencent-id", "tencent-key",
                               "niutrans-id", "niutrans-key", "voice-app", "voice-default", "voice-provider",
                               "polish-default", "polish-provider", "custom-key"})
        REQUIRE(protected_text.find(secret) == std::string::npos);
    const std::string ciphertext = protected_text;
    REQUIRE(ConfigCredentials::ProtectToml(protected_text));
    REQUIRE_EQ(protected_text, ciphertext);
    REQUIRE(ConfigCredentials::UnprotectToml(protected_text));
    REQUIRE(toml::parse(protected_text) == toml::parse(original));
}

TEST_CASE(credential_toml_handles_unicode_multiline_and_inline_tables)
{
    const std::string original = "\"中文\" = { api_key = \"密钥中文\", description = \"保留\" }\n"
                                 "[ai_assistant]\ntoken = \"\"\"first\nsecond\\\"\\\\end\"\"\"\n"
                                 "[[services]]\ntoken = 'array-key'\n";
    std::string text = original;
    REQUIRE(ConfigCredentials::ProtectToml(text));
    REQUIRE(text.find("密钥中文") == std::string::npos);
    REQUIRE(text.find("first\nsecond") == std::string::npos);
    REQUIRE(text.find("array-key") == std::string::npos);
    REQUIRE(ConfigCredentials::UnprotectToml(text));
    REQUIRE(toml::parse(text) == toml::parse(original));
}

TEST_CASE(credential_toml_is_transactional_on_unreadable_values)
{
    for (const char *bad : {"[ai_assistant]\ntoken = 'legacy-secret'\ntoken_glm = 'dpapi:v1:00'\n",
                            "[glm_translation]\napi_key = 'dpapi:v2:00'\n", "[glm_translation]\napi_key = 123\n",
                            "[ai_assistant]\ntoken = \"unterminated"})
    {
        std::string text = bad;
        REQUIRE(!ConfigCredentials::ProtectToml(text));
        REQUIRE_EQ(text, std::string(bad));
        REQUIRE(!ConfigCredentials::UnprotectToml(text));
        REQUIRE_EQ(text, std::string(bad));
    }
}

TEST_CASE(credential_toml_preserves_utf8_bom_and_source_offsets)
{
    const std::string original = "\xef\xbb\xbf"
                                 "api_key = 'bom-secret'\n";
    std::string text = original;
    REQUIRE(ConfigCredentials::ProtectToml(text));
    REQUIRE(text.rfind("\xef\xbb\xbf", 0) == 0);
    REQUIRE(text.find("bom-secret") == std::string::npos);
    REQUIRE(ConfigCredentials::UnprotectToml(text));
    REQUIRE(toml::parse(text) == toml::parse(original));
}

TEST_CASE(credential_toml_leaves_shipped_placeholders_unchanged)
{
    const std::string original = "[ai_assistant]\ntoken = '<YOUR_API_KEY>'\ntoken_glm = ''\n"
                                 "[voice_input]\nasr_token = 'FAKESECRET_example'\n";
    std::string text = original;
    REQUIRE(ConfigCredentials::ProtectToml(text));
    REQUIRE_EQ(text, original);
    REQUIRE(ConfigCredentials::UnprotectToml(text));
    REQUIRE_EQ(text, original);
}
