#pragma once

#include <string>

namespace ofxICExample {

bool executableFileExists(const std::string & path);

std::string resolveWorkbenchPath(
	const std::string & value,
	const std::string & workbenchRoot);

std::string portableWorkbenchPath(
	const std::string & value,
	const std::string & workbenchRoot);

std::string findInstalledExecutable(
	const std::string & serverRoot,
	const std::string & familyPrefix,
	const std::string & executableName,
	const std::string & preferredPath = {});

std::string installedExecutableSearchDiagnostic(
	const std::string & serverRoot,
	const std::string & familyPrefix,
	const std::string & executableName);

std::string resolveInstalledExecutable(
	const std::string & configuredPath,
	const std::string & serverRoot,
	const std::string & familyPrefix,
	const std::string & executableName);

std::string resolveInstalledExecutable(
	const std::string & configuredPath,
	const std::string & startupDetectedPath,
	const std::string & serverRoot,
	const std::string & familyPrefix,
	const std::string & executableName);

} // namespace ofxICExample
