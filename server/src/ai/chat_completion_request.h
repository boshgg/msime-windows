#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace ChatCompletion
{
// Shared by live suggestions, translation and the settings credential probe.
nlohmann::json BuildRequest(const std::string &provider, const std::string &model, const nlohmann::json &messages,
                            int max_tokens, bool json_output);

struct ContentResult
{
    std::string content;
    std::string error;
};

ContentResult ParseContent(const std::string &response);
} // namespace ChatCompletion
