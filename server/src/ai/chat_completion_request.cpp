#include "chat_completion_request.h"

#include <algorithm>
#include <cctype>

namespace ChatCompletion
{
nlohmann::json BuildRequest(const std::string &provider, const std::string &model, const nlohmann::json &messages,
                            int max_tokens, bool json_output)
{
    nlohmann::json body = {{"model", model}, {"stream", false}, {"max_tokens", max_tokens}, {"messages", messages}};
    if (provider == "glm")
    {
        std::string normalized = model;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        // GLM-5.3 (including FlashX) always reasons: disabling thinking is rejected.
        // Leave enough tokens for reasoning before the compact final JSON answer.
        const bool always_thinks = normalized.rfind("glm-5.3", 0) == 0;
        if (always_thinks)
        {
            body["thinking"] = {{"type", "enabled"}};
            body["reasoning_effort"] = "low";
            body["max_tokens"] = (std::max)(max_tokens, 2048);
        }
        else if (normalized.rfind("glm-4.5", 0) == 0 || normalized.rfind("glm-4.6", 0) == 0 ||
                 normalized.rfind("glm-4.7", 0) == 0)
        {
            body["thinking"] = {{"type", "disabled"}};
        }
        body["do_sample"] = false;
        // GLM multimodal endpoints do not uniformly accept response_format.
        // Both callers request JSON in their prompt and strictly validate the result.
    }
    else
    {
        body["temperature"] = 0.2;
        if (json_output)
            body["response_format"] = {{"type", "json_object"}};
        if (provider == "deepseek")
            body["thinking"] = {{"type", "disabled"}};
        else if (provider == "siliconflow")
            body["enable_thinking"] = false;
    }
    return body;
}

ContentResult ParseContent(const std::string &response)
{
    try
    {
        const auto root = nlohmann::json::parse(response);
        const auto &choice = root.at("choices").at(0);
        const auto reason = choice.value("finish_reason", std::string());
        if (reason == "length")
            return {{}, "模型输出被截断，请增加输出预算或选择更快的模型。"};
        if (!reason.empty() && reason != "stop")
            return {{}, "模型没有正常完成回答，请检查服务端模型配置。"};
        const auto &content = choice.at("message").at("content");
        if (!content.is_string() ||
            content.get_ref<const std::string &>().find_first_not_of(" \t\r\n") == std::string::npos)
            return {{}, "服务未返回回答正文，可能只返回了推理内容。"};
        return {content.get<std::string>(), {}};
    }
    catch (...)
    {
        return {{}, "服务返回的 Chat Completions 数据格式无效。"};
    }
}
} // namespace ChatCompletion
