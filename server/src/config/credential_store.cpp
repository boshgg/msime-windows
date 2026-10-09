#include "config/credential_store.h"

#include <Windows.h>
#include <wincrypt.h>
#include <toml++/toml.h>
#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr std::string_view kPrefix = "dpapi:v1:";
// Domain separation, not an encryption key. DPAPI owns and protects the key.
constexpr char kEntropy[] = "MetasequoiaIME/config-credentials/v1";

struct LocalBlob
{
    DATA_BLOB data{};
    ~LocalBlob()
    {
        if (data.pbData)
        {
            SecureZeroMemory(data.pbData, data.cbData);
            LocalFree(data.pbData);
        }
    }
};

DATA_BLOB Entropy()
{
    return {static_cast<DWORD>(sizeof(kEntropy) - 1), reinterpret_cast<BYTE *>(const_cast<char *>(kEntropy))};
}

int HexDigit(char value)
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    return -1;
}

std::string QuoteToml(const std::string &value)
{
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (const unsigned char ch : value)
    {
        if (ch == '\\' || ch == '"')
        {
            result += '\\';
            result += static_cast<char>(ch);
        }
        else if (ch < 0x20 || ch == 0x7f)
        {
            result += "\\u00";
            result += hex[ch >> 4];
            result += hex[ch & 15];
        }
        else
        {
            result += static_cast<char>(ch);
        }
    }
    return result + '"';
}

// toml++ columns count Unicode code points rather than UTF-8 bytes.
size_t ByteOffset(const std::string &text, const toml::source_position &position)
{
    size_t offset = text.rfind("\xef\xbb\xbf", 0) == 0 ? 3 : 0;
    for (size_t line = 1; line < position.line; ++line)
    {
        const size_t newline = text.find('\n', offset);
        if (newline == std::string::npos)
            return std::string::npos;
        offset = newline + 1;
    }
    for (size_t column = 1; column < position.column; ++column)
    {
        if (offset >= text.size() || text[offset] == '\n')
            return std::string::npos;
        ++offset;
        while (offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xc0) == 0x80)
            ++offset;
    }
    return offset;
}

struct Patch
{
    size_t begin;
    size_t end;
    std::string value;
};

bool CollectPatches(const toml::node &node, const std::string &text, bool protect, std::vector<Patch> &patches)
{
    if (const auto *table = node.as_table())
    {
        for (const auto &[key, value] : *table)
        {
            if (!ConfigCredentials::IsCredentialKey(key.str()))
            {
                if (!CollectPatches(value, text, protect, patches))
                    return false;
                continue;
            }
            const auto *string = value.as_string();
            if (!string)
                return false;
            const std::string &stored = string->get();
            std::string replacement;
            if (ConfigCredentials::IsEncrypted(stored))
            {
                // Validate even on save; never accept or overwrite unreadable ciphertext.
                if (!ConfigCredentials::Decrypt(stored, replacement))
                    return false;
                if (protect)
                    continue;
            }
            else
            {
                // Empty and shipped placeholder values are not credentials. Keeping
                // them literal also preserves the installed-template baseline.
                if (!protect || stored.empty() || (stored.front() == '<' && stored.back() == '>') ||
                    stored.rfind("FAKESECRET_", 0) == 0)
                    continue;
                if (!ConfigCredentials::Encrypt(stored, replacement))
                    return false;
            }
            const size_t begin = ByteOffset(text, value.source().begin);
            const size_t end = ByteOffset(text, value.source().end);
            if (begin == std::string::npos || end == std::string::npos || begin >= end || end > text.size())
                return false;
            patches.push_back({begin, end, QuoteToml(replacement)});
        }
    }
    else if (const auto *array = node.as_array())
    {
        for (const auto &value : *array)
            if (!CollectPatches(value, text, protect, patches))
                return false;
    }
    return true;
}

bool TransformToml(std::string &text, bool protect)
{
    try
    {
        const auto table = toml::parse(text);
        std::vector<Patch> patches;
        if (!CollectPatches(table, text, protect, patches))
            return false;
        std::sort(patches.begin(), patches.end(), [](const Patch &a, const Patch &b) { return a.begin > b.begin; });
        std::string result = text;
        for (const auto &patch : patches)
            result.replace(patch.begin, patch.end - patch.begin, patch.value);
        // Reject offset/encoding mistakes before anything is written.
        (void)toml::parse(result);
        text = std::move(result);
        return true;
    }
    catch (const toml::parse_error &)
    {
        return false;
    }
}
} // namespace

namespace ConfigCredentials
{
bool IsCredentialKey(std::string_view key)
{
    return key == "token" || key == "api_key" || key == "apikey" || key == "api_secret" || key == "secret_id" ||
           key == "secret_key" || key == "password" || key == "app_id" || key == "asr_app_key" || key == "asr_token" ||
           key == "polish_token" || key.rfind("token_", 0) == 0 || key.rfind("asr_token_", 0) == 0 ||
           key.rfind("polish_token_", 0) == 0;
}

bool IsEncrypted(std::string_view value)
{
    // Reserve the whole namespace so unknown future versions fail closed.
    return value.rfind("dpapi:", 0) == 0;
}

bool Encrypt(const std::string &plaintext, std::string &ciphertext)
{
    if (plaintext.size() > (std::numeric_limits<DWORD>::max)())
        return false;
    DATA_BLOB input{static_cast<DWORD>(plaintext.size()),
                    reinterpret_cast<BYTE *>(const_cast<char *>(plaintext.data()))};
    DATA_BLOB entropy = Entropy();
    LocalBlob output;
    if (!CryptProtectData(&input, L"MetasequoiaIME credential", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                          &output.data))
        return false;
    constexpr char hex[] = "0123456789abcdef";
    std::string result(kPrefix);
    result.reserve(kPrefix.size() + 2 * static_cast<size_t>(output.data.cbData));
    for (DWORD i = 0; i < output.data.cbData; ++i)
    {
        result += hex[output.data.pbData[i] >> 4];
        result += hex[output.data.pbData[i] & 15];
    }
    ciphertext = std::move(result);
    return true;
}

bool Decrypt(const std::string &ciphertext, std::string &plaintext)
{
    if (ciphertext.rfind(kPrefix, 0) != 0)
        return false;
    const size_t size = ciphertext.size() - kPrefix.size();
    if (size == 0 || size % 2 != 0 || size / 2 > (std::numeric_limits<DWORD>::max)())
        return false;
    std::vector<BYTE> bytes(size / 2);
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        const int high = HexDigit(ciphertext[kPrefix.size() + 2 * i]);
        const int low = HexDigit(ciphertext[kPrefix.size() + 2 * i + 1]);
        if (high < 0 || low < 0)
            return false;
        bytes[i] = static_cast<BYTE>((high << 4) | low);
    }
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), bytes.data()};
    DATA_BLOB entropy = Entropy();
    LocalBlob output;
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output.data))
        return false;
    plaintext.assign(reinterpret_cast<const char *>(output.data.pbData), output.data.cbData);
    return true;
}

bool ProtectToml(std::string &text)
{
    return TransformToml(text, true);
}

bool UnprotectToml(std::string &text)
{
    return TransformToml(text, false);
}
} // namespace ConfigCredentials
