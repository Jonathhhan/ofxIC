#pragma once

#include <string>

namespace ofxICExample {

bool credentialStoreAvailable();
bool saveCredential(
	const std::string & name,
	const std::string & secret,
	std::string & error);
bool loadCredential(
	const std::string & name,
	std::string & secret,
	std::string & error);
bool deleteCredential(const std::string & name, std::string & error);

} // namespace ofxICExample
