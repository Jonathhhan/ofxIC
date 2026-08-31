#include "ExampleRuntimePaths.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace ofxICExample {
namespace {

std::string normalizedPath(const std::filesystem::path & path) {
	std::error_code error;
	const auto absolute = std::filesystem::absolute(path, error);
	return (error ? path : absolute).lexically_normal().string();
}

std::string lowerAscii(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
	return value;
}

std::string byteUnits(const std::string & value) {
	std::ostringstream result;
	result << std::hex << std::setfill('0');
	for (std::size_t index = 0; index < value.size(); ++index) {
		if (index) result << ',';
		result << std::setw(2) << static_cast<unsigned int>(
			static_cast<unsigned char>(value[index]));
	}
	return result.str();
}

bool startsWithIgnoreCase(const std::string & value, const std::string & prefix) {
	if (value.size() < prefix.size()) return false;
	return lowerAscii(value.substr(0, prefix.size())) == lowerAscii(prefix);
}

bool filenameEquals(const std::filesystem::path & path, const std::string & name) {
#if defined(_WIN32)
	return lowerAscii(path.filename().string()) == lowerAscii(name);
#else
	return path.filename() == name;
#endif
}

struct Candidate {
	std::filesystem::path path;
	std::filesystem::file_time_type modified{};
};

#if defined(_WIN32)
struct NativeCandidate {
	std::filesystem::path path;
	std::uint64_t modified = 0;
};

std::string wideUnits(const std::wstring & value) {
	std::ostringstream result;
	result << std::hex << std::setfill('0');
	for (std::size_t index = 0; index < value.size(); ++index) {
		if (index) result << ',';
		result << std::setw(4) << static_cast<unsigned int>(value[index]);
	}
	return result.str();
}

bool startsWithIgnoreCase(const std::wstring & value, const std::wstring & prefix) {
	return value.size() >= prefix.size() &&
		_wcsnicmp(value.c_str(), prefix.c_str(), prefix.size()) == 0;
}

std::uint64_t fileTimeValue(const FILETIME & time) {
	ULARGE_INTEGER value{};
	value.LowPart = time.dwLowDateTime;
	value.HighPart = time.dwHighDateTime;
	return value.QuadPart;
}

void collectNativeCandidates(const std::filesystem::path & directory,
	const std::wstring & executableName, std::vector<NativeCandidate> & candidates) {
	WIN32_FIND_DATAW entry{};
	const std::wstring pattern = (directory / L"*").wstring();
	HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
	if (search == INVALID_HANDLE_VALUE) return;
	do {
		const std::wstring name(entry.cFileName);
		if (name == L"." || name == L"..") continue;
		const auto path = directory / name;
		if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
			if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
				collectNativeCandidates(path, executableName, candidates);
		} else if (_wcsicmp(name.c_str(), executableName.c_str()) == 0) {
			candidates.push_back({ path, fileTimeValue(entry.ftLastWriteTime) });
		}
	} while (FindNextFileW(search, &entry));
	FindClose(search);
}

std::string findInstalledExecutableNative(const std::filesystem::path & root,
	const std::string & familyPrefix, const std::string & executableName) {
	const std::wstring widePrefix = std::filesystem::path(familyPrefix).wstring();
	const std::wstring wideExecutable = std::filesystem::path(executableName).wstring();
	WIN32_FIND_DATAW entry{};
	const std::wstring pattern = (root / L"*").wstring();
	HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
	if (search == INVALID_HANDLE_VALUE) return {};
	std::vector<NativeCandidate> candidates;
	do {
		const std::wstring name(entry.cFileName);
		if (name == L"." || name == L".." ||
			(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
			!startsWithIgnoreCase(name, widePrefix)) continue;
		const auto familyRoot = root / name;
		WIN32_FILE_ATTRIBUTE_DATA direct{};
		const auto installed = familyRoot / wideExecutable;
		if (GetFileAttributesExW(installed.c_str(), GetFileExInfoStandard, &direct) &&
			(direct.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
			candidates.push_back({ installed, fileTimeValue(direct.ftLastWriteTime) });
			continue;
		}
		collectNativeCandidates(familyRoot, wideExecutable, candidates);
	} while (FindNextFileW(search, &entry));
	FindClose(search);
	if (candidates.empty()) return {};
	std::sort(candidates.begin(), candidates.end(), [](const NativeCandidate & left,
		const NativeCandidate & right) {
		if (left.modified != right.modified) return left.modified < right.modified;
		return left.path.generic_wstring() < right.path.generic_wstring();
	});
	return normalizedPath(candidates.back().path);
}
#endif

} // namespace

bool executableFileExists(const std::string & path) {
	if (path.empty()) return false;
#if defined(_WIN32)
	const std::filesystem::path nativePath(path);
	const DWORD attributes = GetFileAttributesW(nativePath.c_str());
	if (attributes != INVALID_FILE_ATTRIBUTES)
		return (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#endif
	std::error_code error;
	return std::filesystem::is_regular_file(std::filesystem::path(path), error);
}

std::string findInstalledExecutable(
	const std::string & serverRoot,
	const std::string & familyPrefix,
	const std::string & executableName) {
	if (serverRoot.empty() || familyPrefix.empty() || executableName.empty()) return {};
	const std::filesystem::path root(serverRoot);
#if defined(_WIN32)
	// Prefer the native API on Windows. Some sandboxed/medium-integrity GUI
	// launches have returned ERROR_PATH_NOT_FOUND through the MSVC
	// std::filesystem probe for an otherwise enumerable LocalAppData tree.
	// The previous fallback sat after that probe and therefore never ran for
	// exactly this failure mode.
	const std::string native = findInstalledExecutableNative(
		root, familyPrefix, executableName);
	if (!native.empty()) return native;
#endif
	std::error_code error;
	if (!std::filesystem::is_directory(root, error)) return {};

	std::vector<std::filesystem::path> familyRoots;
	for (std::filesystem::directory_iterator iterator(
		root, std::filesystem::directory_options::skip_permission_denied, error), end;
		iterator != end;) {
		if (error) {
			error.clear();
			iterator.increment(error);
			continue;
		}
		std::error_code entryError;
		if (iterator->is_directory(entryError) && !entryError &&
			startsWithIgnoreCase(iterator->path().filename().string(), familyPrefix))
			familyRoots.push_back(iterator->path());
		iterator.increment(error);
	}

	std::vector<Candidate> candidates;
	for (const auto & familyRoot : familyRoots) {
		// Current installers place the stable launcher directly in the versioned
		// install root. Prefer it over compiler output retained below build/ so a
		// later incremental build cannot silently change the executable selected
		// by the GUI. Recursive discovery remains a compatibility fallback for
		// older layouts.
		const std::filesystem::path installedExecutable = familyRoot / executableName;
		std::error_code installedError;
		if (std::filesystem::is_regular_file(installedExecutable, installedError) &&
			!installedError) {
			Candidate candidate;
			candidate.path = installedExecutable;
			candidate.modified = std::filesystem::last_write_time(
				installedExecutable, installedError);
			if (installedError)
				candidate.modified = std::filesystem::file_time_type::min();
			candidates.push_back(std::move(candidate));
			continue;
		}
		std::vector<std::filesystem::path> pending{ familyRoot };
		while (!pending.empty()) {
			const std::filesystem::path directory = std::move(pending.back());
			pending.pop_back();
			std::error_code directoryError;
			for (std::filesystem::directory_iterator iterator(
				directory, std::filesystem::directory_options::skip_permission_denied,
				directoryError), end; iterator != end;) {
				if (directoryError) {
					directoryError.clear();
					iterator.increment(directoryError);
					continue;
				}
				std::error_code entryError;
				if (iterator->is_directory(entryError) && !entryError) {
					pending.push_back(iterator->path());
				} else if (!entryError && iterator->is_regular_file(entryError) &&
					!entryError && filenameEquals(iterator->path(), executableName)) {
					Candidate candidate;
					candidate.path = iterator->path();
					candidate.modified = iterator->last_write_time(entryError);
					if (entryError)
						candidate.modified = std::filesystem::file_time_type::min();
					candidates.push_back(std::move(candidate));
				}
				iterator.increment(directoryError);
			}
		}
	}
	if (candidates.empty()) return {};
	std::sort(candidates.begin(), candidates.end(), [](const Candidate & left,
		const Candidate & right) {
		if (left.modified != right.modified) return left.modified < right.modified;
		return left.path.generic_string() < right.path.generic_string();
	});
	return normalizedPath(candidates.back().path);
}

std::string installedExecutableSearchDiagnostic(
	const std::string & serverRoot,
	const std::string & familyPrefix,
	const std::string & executableName) {
	std::ostringstream detail;
	detail << "root=" << (serverRoot.empty() ? "<empty>" : serverRoot);
	if (serverRoot.empty()) return detail.str() + "; root_state=empty";
	const std::filesystem::path root(serverRoot);
	std::error_code error;
	const bool directory = std::filesystem::is_directory(root, error);
	detail << "; root_state=" << (directory ? "directory" :
		(error ? "error-" + std::to_string(error.value()) : "missing"));
#if defined(_WIN32)
	const DWORD nativeAttributes = GetFileAttributesW(root.c_str());
	const DWORD nativeError = nativeAttributes == INVALID_FILE_ATTRIBUTES
		? GetLastError() : ERROR_SUCCESS;
	const bool nativeDirectory = nativeAttributes != INVALID_FILE_ATTRIBUTES &&
		(nativeAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	const std::string native = findInstalledExecutableNative(
		root, familyPrefix, executableName);
	detail << "; native_root_state=" << (nativeDirectory ? "directory" :
		("error-" + std::to_string(nativeError)))
		<< "; native_result=" << (native.empty() ? "none" : native);
	if (!nativeDirectory) {
		const std::wstring nativeRoot = root.native();
		detail << "; root_bytes_length=" << serverRoot.size()
			<< "; root_bytes=" << byteUnits(serverRoot)
			<< "; root_native_length=" << nativeRoot.size()
			<< "; root_native_units=" << wideUnits(nativeRoot);
	}
#endif
	if (!directory) return detail.str();

	std::size_t familyDirectories = 0;
	std::size_t matchingFiles = 0;
	for (std::filesystem::recursive_directory_iterator iterator(root,
		std::filesystem::directory_options::skip_permission_denied, error), end;
		iterator != end;) {
		if (error) {
			error.clear();
			iterator.increment(error);
			continue;
		}
		std::error_code entryError;
		if (iterator.depth() == 0 && iterator->is_directory(entryError) && !entryError &&
			startsWithIgnoreCase(iterator->path().filename().string(), familyPrefix))
			++familyDirectories;
		if (iterator->is_regular_file(entryError) && !entryError &&
			filenameEquals(iterator->path(), executableName)) {
			const auto relative = iterator->path().lexically_relative(root);
			if (!relative.empty() && startsWithIgnoreCase(
				relative.begin()->string(), familyPrefix)) ++matchingFiles;
		}
		iterator.increment(error);
	}
	detail << "; family_directories=" << familyDirectories
		<< "; matching_files=" << matchingFiles;
	if (error) detail << "; traversal_error=" << error.value();
	return detail.str();
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

std::string resolveInstalledExecutable(
	const std::string & configuredPath,
	const std::string & startupDetectedPath,
	const std::string & serverRoot,
	const std::string & familyPrefix,
	const std::string & executableName) {
	if (executableFileExists(configuredPath))
		return normalizedPath(std::filesystem::path(configuredPath));
	// A startup path supplied by the pinned installer layout is authoritative.
	// The Windows process launcher will report the real CreateProcess error if
	// the runtime was removed after startup; do not discard the path because a
	// preliminary filesystem probe cannot enumerate LocalAppData.
	if (!startupDetectedPath.empty())
		return normalizedPath(std::filesystem::path(startupDetectedPath));
	return findInstalledExecutable(serverRoot, familyPrefix, executableName);
}

} // namespace ofxICExample
