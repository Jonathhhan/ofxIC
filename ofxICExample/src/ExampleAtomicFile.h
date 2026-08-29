#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ofxICExample {
namespace detail {

inline std::uint64_t processIdForTemporaryFile() {
#if defined(_WIN32)
	return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
	return static_cast<std::uint64_t>(getpid());
#endif
}

inline std::filesystem::path uniqueTemporaryPath(
	const std::filesystem::path & target) {
	static std::atomic<std::uint64_t> sequence{ 0 };
	const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
	return target.string() + ".tmp." + std::to_string(processIdForTemporaryFile()) +
		"." + std::to_string(ticks) + "." +
		std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

inline void removeTemporaryFile(const std::filesystem::path & path) {
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

} // namespace detail

// Writes beside the destination and replaces it only after the complete payload
// has reached the temporary file. A failed replacement preserves an existing
// destination and removes the temporary file.
inline bool writeTextFileAtomically(const std::string & path,
	const std::string & contents, std::string * errorMessage = nullptr) {
	const auto fail = [errorMessage](const std::string & message) {
		if (errorMessage) *errorMessage = message;
		return false;
	};
	if (path.empty()) return fail("The destination path is empty.");
	if (contents.size() > static_cast<std::size_t>(
		(std::numeric_limits<std::streamsize>::max)())) {
		return fail("The payload is too large to write.");
	}

	const std::filesystem::path target(path);
	std::error_code filesystemError;
	if (!target.parent_path().empty()) {
		std::filesystem::create_directories(target.parent_path(), filesystemError);
		if (filesystemError) return fail("Could not create the destination folder.");
	}
	const std::filesystem::path temporary = detail::uniqueTemporaryPath(target);
	std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
	if (!output) return fail("Could not create the temporary output file.");
	output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
	output.close();
	if (!output) {
		detail::removeTemporaryFile(temporary);
		return fail("Could not complete the temporary output file.");
	}

#if defined(_WIN32)
	if (!MoveFileExW(temporary.wstring().c_str(), target.wstring().c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		const DWORD error = GetLastError();
		detail::removeTemporaryFile(temporary);
		return fail("Could not atomically replace the destination (Windows error " +
			std::to_string(error) + ").");
	}
#else
	std::filesystem::rename(temporary, target, filesystemError);
	if (filesystemError) {
		detail::removeTemporaryFile(temporary);
		return fail("Could not atomically replace the destination.");
	}
#endif
	if (errorMessage) errorMessage->clear();
	return true;
}

} // namespace ofxICExample
