#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace GlmTranslation
{
struct Config
{
    std::string endpoint;
    std::string api_key;
    std::string model;
};

bool IsUsableConfig(const Config &config);
nlohmann::json BuildRequest(const Config &config, const std::vector<std::string> &texts, const std::string &source,
                            const std::string &target);
// Every result carries its original index. Duplicate, missing or out-of-range IDs reject the entire batch.
std::vector<std::string> ParseTranslationResponse(const std::string &response, size_t expected_count,
                                                  std::string *error = nullptr);
std::vector<std::string> TextTranslateBatch(const Config &config, const std::vector<std::string> &texts,
                                            const std::string &source, const std::string &target,
                                            std::string *error = nullptr, const std::function<bool()> &cancelled = {});
} // namespace GlmTranslation
