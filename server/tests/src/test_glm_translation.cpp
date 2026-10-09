#include "tests/includes/test_framework.h"

#include "cloud/glm_translation.h"

namespace
{
std::string Response(const nlohmann::json &translations, const std::string &reason = "stop")
{
    const nlohmann::json content = {{"translations", translations}};
    return nlohmann::json{{"choices", {{{"finish_reason", reason}, {"message", {{"content", content.dump()}}}}}}}
        .dump();
}
} // namespace

TEST_CASE(GlmTranslationBatchesCandidatesWithStableIds)
{
    const GlmTranslation::Config config{"https://open.bigmodel.cn/api/paas/v4/chat/completions", "test-key",
                                        "glm-5.3-flashx"};
    const auto body = GlmTranslation::BuildRequest(config, {"测试", "输入法"}, "zh", "en");
    REQUIRE_EQ(body.at("model"), "glm-5.3-flashx");
    REQUIRE_EQ(body.at("thinking").at("type"), "enabled");
    REQUIRE_EQ(body.at("reasoning_effort"), "low");
    REQUIRE_EQ(body.at("max_tokens"), 2048);
    REQUIRE_EQ(body.at("do_sample"), false);
    const auto input = nlohmann::json::parse(body.at("messages").at(1).at("content").get<std::string>());
    REQUIRE_EQ(input.at("items").size(), size_t(2));
    REQUIRE_EQ(input.at("items").at(0).at("id"), 0);
    REQUIRE_EQ(input.at("items").at(1).at("text"), "输入法");
    REQUIRE_EQ(input.at("source_language"), "zh");
    REQUIRE_EQ(input.at("target_language"), "en");
}

TEST_CASE(GlmTranslationAlignsReorderedResults)
{
    std::string error;
    const auto result = GlmTranslation::ParseTranslationResponse(
        Response({{{"id", 1}, {"text", "input method"}}, {{"id", 0}, {"text", "test"}}}), 2, &error);
    REQUIRE(error.empty());
    REQUIRE_EQ(result.size(), size_t(2));
    REQUIRE_EQ(result[0], std::string("test"));
    REQUIRE_EQ(result[1], std::string("input method"));
}

TEST_CASE(GlmTranslationRejectsMissingDuplicateWrongAndExcessIds)
{
    const std::vector<nlohmann::json> invalid{
        {{{"id", 0}, {"text", "test"}}},
        {{{"id", 0}, {"text", "test"}}, {{"id", 0}, {"text", "other"}}},
        {{{"id", 0}, {"text", "test"}}, {{"id", 2}, {"text", "other"}}},
        {{{"id", -1}, {"text", "test"}}, {{"id", 1}, {"text", "other"}}},
        {{{"id", 0}, {"text", "test"}}, {{"id", "1"}, {"text", "other"}}},
        {{{"id", 0}, {"text", "test"}}, {{"id", 1}, {"text", 12}}},
        {{{"id", 0}, {"text", "test"}}, {{"id", 1}, {"text", std::string(513, 'x')}}},
        {"test", "input method"},
    };
    for (const auto &items : invalid)
    {
        std::string error;
        const auto result = GlmTranslation::ParseTranslationResponse(Response(items), 2, &error);
        REQUIRE(!error.empty());
        REQUIRE_EQ(result.size(), size_t(2));
        REQUIRE(result[0].empty());
        REQUIRE(result[1].empty());
    }
}

TEST_CASE(GlmTranslationRejectsTruncatedOrMalformedJson)
{
    std::string error;
    auto result =
        GlmTranslation::ParseTranslationResponse(Response({{{"id", 0}, {"text", "test"}}}, "length"), 1, &error);
    REQUIRE(!error.empty());
    REQUIRE(result[0].empty());
    result = GlmTranslation::ParseTranslationResponse("bad JSON", 1, &error);
    REQUIRE(!error.empty());
    REQUIRE(result[0].empty());
}

TEST_CASE(GlmTranslationValidatesConfigurationAndCancellationBeforeNetwork)
{
    GlmTranslation::Config config{"https://open.bigmodel.cn/api/paas/v4/chat/completions", "test-key",
                                  "glm-5.3-flashx"};
    REQUIRE(GlmTranslation::IsUsableConfig(config));
    std::string error;
    const auto result = GlmTranslation::TextTranslateBatch(config, {"测试"}, "zh", "en", &error, [] { return true; });
    REQUIRE(!error.empty());
    REQUIRE_EQ(result.size(), size_t(1));
    REQUIRE(result[0].empty());
    config.api_key = "<YOUR_GLM_API_KEY>";
    REQUIRE(!GlmTranslation::IsUsableConfig(config));
    config.api_key = "key\r\nInjected: header";
    REQUIRE(!GlmTranslation::IsUsableConfig(config));
    config.api_key = "test-key";
    config.endpoint = "http://example.com/chat/completions";
    REQUIRE(!GlmTranslation::IsUsableConfig(config));
    config.endpoint = "file:///test";
    REQUIRE(!GlmTranslation::IsUsableConfig(config));
}
