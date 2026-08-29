#include "ExampleRuntimePaths.h"

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <vector>

namespace ofxICExample {
namespace {

std::string normalizedPath(const std::filesystem::path & path) {
	std::error_code error;
	const auto absolute = std::filesystem::absolute(path, error);
	return (error ? path : absolute).lexically_normal().string();
}

bool belongsToFamily(
	const std::filesystem::path & relative,
	const std::string & familyPrefix) {
	for (const auto & component : relative) {
		const std::string name = component.string();
		if (name.rfind(familyPrefix, 0) == 0) return true;
	}
	return false;
}

struct Candidate {
	std::filesystem::path path;
	std::filesystem::file_time_type modified{};
};

} // namespace

bool executableFileExists(const std::string & path) {
	if (path.empty()) return false;
	std::error_code error;
	return std::filesystem::is_regular_file(std::filesystem::path(path), error);
}

std::string findInstalledExecutable(
	const std::string & serverRoot,
	const std::string & familyPrefix,
	const std::string & executableName) {
	if (serverRoot.empty() || familyPrefix.empty() || executableName.empty()) return {};
	const std::filesystem::path root(serverRoot);
	std::error_code error;
	if (!std::filesystem::is_directory(root, error)) return {};

	std::vector<Candidate> candidates;
	for (std::filesystem::recursive_directory_iterator iterator(
		root, std::filesystem::directory_options::skip_permission_denied, error), end;
		iterator != end; iterator.increment(error)) {
		if (error) {
			error.clear();
			continue;
		}
		std::error_code entryError;
		if (!iterator->is_regular_file(entryError) ||
			iterator->path().filename() != executableName) continue;
		const auto relative = iterator->path().lexically_relative(root);
		if (!belongsToFamily(relative, familyPrefix)) continue;
		Candidate candidate;
		candidate.path = iterator->path();
		candidate.modified = iterator->last_write_time(entryError);
		if (entryError) candidate.modified = std::filesystem::file_time_type::min();
		candidates.push_back(std::move(candidate));
	}
	if (candidates.empty()) return {};
	std::sort(candidates.begin(), candidates.end(), [](const Candidate & left,
		const Candidate & right) {
		if (left.modified != right.modified) return left.modified < right.modified;
		return left.path.generic_string() < right.path.generic_string();
	});
	return normalizedPath(candidates.back().path);
}

std::string resolveInstalledExecutable(
	const std::string & configuredPath,
	const std::string & serverRoot,
	const std::string & familyPrefix,
	const std::string & executableName) {
	if (executableFileExists(configuredPath))
		return normalizedPath(std::filesystem::path(configuredPath));
	return findInstalledExecutable(serverRoot, familyPrefix, executableName);
}

} // namespace ofxICExample
