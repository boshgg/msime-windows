#pragma once

#include <string>
#include <string_view>

namespace ConfigCredentials
{
// Windows DPAPI binds these values to the current Windows user. To recover an
// inaccessible account's config, keep a backup, manually clear its inaccessible
// dpapi credential values, then reopen settings and enter the credentials again.
bool IsCredentialKey(std::string_view key);
bool IsEncrypted(std::string_view value);
bool Encrypt(const std::string &plaintext, std::string &ciphertext);
bool Decrypt(const std::string &ciphertext, std::string &plaintext);

// Transform every credential in a complete TOML document, retaining formatting.
// Both are transactional: malformed TOML, unknown envelope versions, and failed
// DPAPI operations return false without modifying text. Protect also validates
// existing ciphertext, so an unreadable credential can never be silently lost.
bool ProtectToml(std::string &text);
bool UnprotectToml(std::string &text);
} // namespace ConfigCredentials
