#include "tests/includes/test_framework.h"

#include "ai/chat_completion_request.h"

TEST_CASE(Glm53KeepsRequiredThinkingAndReasoningBudget)
{
    const nlohmann::json messages = {{{"role", "user"}, {"content", "Reply OK"}}};
    for (const auto model : {"glm-5.3", "glm-5.3-flashx", "GLM-5.3"})
    {
        const auto body = ChatCompletion::BuildRequest("glm", model, messages, 512, true);
        REQUIRE_EQ(body.at("thinking").at("type"), "enabled");
        REQUIRE_EQ(body.at("reasoning_effort"), "low");
        REQUIRE(body.at("max_tokens").get<int>() >= 2048);
        REQUIRE_EQ(body.at("do_sample"), false);
        REQUIRE_EQ(body.at("stream"), false);
        REQUIRE(!body.contains("response_format"));
        REQUIRE_EQ(body.at("messages"), messages);
    }
    const auto body = ChatCompletion::BuildRequest("glm", "glm-5.3", messages, 4096, true);
    REQUIRE_EQ(body.at("max_tokens"), 4096);
}

TEST_CASE(GlmChatPolicyPreservesOtherProviderRequests)
{
    const auto body = ChatCompletion::BuildRequest("deepseek", "deepseek-v4-flash", {}, 512, true);
    REQUIRE_EQ(body.at("thinking").at("type"), "disabled");
    REQUIRE_EQ(body.at("response_format").at("type"), "json_object");
    REQUIRE_EQ(body.at("max_tokens"), 512);
    REQUIRE(!body.contains("do_sample"));
    REQUIRE(!body.contains("reasoning_effort"));
    const auto custom = ChatCompletion::BuildRequest("custom", "model", {}, 512, true);
    REQUIRE(!custom.contains("thinking"));
    const auto older = ChatCompletion::BuildRequest("glm", "glm-4.7-flash", {}, 512, true);
    REQUIRE_EQ(older.at("thinking").at("type"), "disabled");
    const auto legacy = ChatCompletion::BuildRequest("glm", "glm-4-flash", {}, 512, true);
    REQUIRE(!legacy.contains("thinking"));
}

TEST_CASE(GlmChatRejectsTruncatedReasoningOnlyOrMalformedAnswers)
{
    const auto ok =
        ChatCompletion::ParseContent(R"({"choices":[{"finish_reason":"stop","message":{"content":"OK"}}]})");
    REQUIRE_EQ(ok.content, std::string("OK"));
    REQUIRE(ok.error.empty());
    for (const auto response :
         {R"({"choices":[{"finish_reason":"length","message":{"content":"partial"}}]})",
          R"({"choices":[{"finish_reason":"stop","message":{"content":"","reasoning_content":"thinking"}}]})",
          R"({"choices":[{"finish_reason":"stop","message":{"content":null}}]})",
          R"({"choices":[{"finish_reason":"content_filter","message":{"content":"blocked"}}]})", R"({"choices":[]})",
          "invalid JSON"})
    {
        const auto result = ChatCompletion::ParseContent(response);
        REQUIRE(result.content.empty());
        REQUIRE(!result.error.empty());
    }
}

TEST_CASE(GlmChatDoesNotEchoUntrustedFinishReason)
{
    const auto result = ChatCompletion::ParseContent(
        R"({"choices":[{"finish_reason":"echoed-secret","message":{"content":"ignored"}}]})");
    REQUIRE(!result.error.empty());
    REQUIRE(result.error.find("echoed-secret") == std::string::npos);
}
