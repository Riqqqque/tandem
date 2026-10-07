// SPDX-License-Identifier: GPL-2.0-or-later
#include "secrets.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wincred.h>
#pragma comment(lib, "advapi32.lib")
#endif

namespace tandem {

#ifdef _WIN32

static std::wstring Widen(const std::string &s)
{
	if (s.empty())
		return {};
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
	std::wstring w(n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
	return w;
}

static std::wstring TargetName(const std::string &name)
{
	return L"Tandem/" + Widen(name);
}

bool SecretStoreAvailable()
{
	return true;
}

bool SecretSet(const std::string &name, const std::string &value)
{
	if (value.empty()) {
		SecretRemove(name);
		return true;
	}
	if (value.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
		return false;

	auto target = TargetName(name);
	CREDENTIALW cred = {};
	cred.Type = CRED_TYPE_GENERIC;
	cred.TargetName = target.data();
	cred.CredentialBlobSize = (DWORD)value.size();
	cred.CredentialBlob = (LPBYTE)value.data();
	cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
	wchar_t user[] = L"Tandem";
	cred.UserName = user;
	return CredWriteW(&cred, 0) != FALSE;
}

std::optional<std::string> SecretGet(const std::string &name)
{
	PCREDENTIALW cred = nullptr;
	if (!CredReadW(TargetName(name).c_str(), CRED_TYPE_GENERIC, 0, &cred))
		return std::nullopt;
	std::string value((const char *)cred->CredentialBlob, cred->CredentialBlobSize);
	SecureZeroMemory(cred->CredentialBlob, cred->CredentialBlobSize);
	CredFree(cred);
	return value;
}

void SecretRemove(const std::string &name)
{
	CredDeleteW(TargetName(name).c_str(), CRED_TYPE_GENERIC, 0);
}

#else

// macOS Keychain and libsecret are not wired up yet; callers fall back to the profile config.
bool SecretStoreAvailable()
{
	return false;
}

bool SecretSet(const std::string &, const std::string &)
{
	return false;
}

std::optional<std::string> SecretGet(const std::string &)
{
	return std::nullopt;
}

void SecretRemove(const std::string &) {}

#endif

} // namespace tandem
