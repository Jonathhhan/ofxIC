#include "ExampleCredentialStore.h"

#if defined(_WIN32)
#include <windows.h>
#include <wincred.h>
#pragma comment(lib, "Advapi32.lib")
#endif

namespace ofxICExample {
namespace {

std::string targetName(const std::string & name) {
	return "ofxICExample/" + name;
}

#if defined(_WIN32)
std::string windowsError(const char * operation) {
	return std::string(operation) + " failed with Windows error " +
		std::to_string(GetLastError());
}
#endif

} // namespace

bool credentialStoreAvailable() {
#if defined(_WIN32)
	return true;
#else
	return false;
#endif
}

bool saveCredential(
	const std::string & name,
	const std::string & secret,
	std::string & error) {
	error.clear();
	if (name.empty() || secret.empty()) {
		error = "credential name and token must not be empty";
		return false;
	}
#if defined(_WIN32)
	if (secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
		error = "token is too large for Windows Credential Manager";
		return false;
	}
	std::string target = targetName(name);
	CREDENTIALA credential{};
	credential.Type = CRED_TYPE_GENERIC;
	credential.TargetName = target.data();
	credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
	credential.CredentialBlob = reinterpret_cast<LPBYTE>(
		const_cast<char *>(secret.data()));
	credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
	credential.UserName = const_cast<char *>("ofxICExample");
	if (!CredWriteA(&credential, 0)) {
		error = windowsError("CredWrite");
		return false;
	}
	return true;
#else
	error = "secure credential storage is not available on this platform";
	return false;
#endif
}

bool loadCredential(
	const std::string & name,
	std::string & secret,
	std::string & error) {
	secret.clear();
	error.clear();
#if defined(_WIN32)
	const std::string target = targetName(name);
	PCREDENTIALA credential = nullptr;
	if (!CredReadA(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
		if (GetLastError() == ERROR_NOT_FOUND) return true;
		error = windowsError("CredRead");
		return false;
	}
	if (credential->CredentialBlob && credential->CredentialBlobSize > 0) {
		secret.assign(
			reinterpret_cast<const char *>(credential->CredentialBlob),
			credential->CredentialBlobSize);
	}
	CredFree(credential);
	return true;
#else
	error = "secure credential storage is not available on this platform";
	return false;
#endif
}

bool deleteCredential(const std::string & name, std::string & error) {
	error.clear();
#if defined(_WIN32)
	const std::string target = targetName(name);
	if (!CredDeleteA(target.c_str(), CRED_TYPE_GENERIC, 0) &&
		GetLastError() != ERROR_NOT_FOUND) {
		error = windowsError("CredDelete");
		return false;
	}
	return true;
#else
	error = "secure credential storage is not available on this platform";
	return false;
#endif
}

} // namespace ofxICExample
