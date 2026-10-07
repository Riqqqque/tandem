// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - storage for stream keys, passwords and API keys.
#pragma once

#include <optional>
#include <string>

namespace tandem {

// True when secrets go to the OS credential store (Windows Credential Manager).
// Elsewhere the caller keeps secrets in the profile's JSON config instead.
bool SecretStoreAvailable();

// name is an opaque identifier such as "target/<id>/key". Values are UTF-8.
bool SecretSet(const std::string &name, const std::string &value);
std::optional<std::string> SecretGet(const std::string &name);
void SecretRemove(const std::string &name);

} // namespace tandem
