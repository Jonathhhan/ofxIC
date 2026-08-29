#pragma once

#include <string>

namespace ofxICExample {

bool executableFileExists(const std::string & path);

std::string findInstalledExecutable(
	const std::string & serverRoot,
	const std::string & familyPrefix,
	const std::string & executableName);

std::string resolveInstalledExecutable(
	const std::string & configuredPath,
	const std::string & serverRoot,
	const std::string & familyPrefix,
	const std::string & executableName);

} // namespace ofxICExample
