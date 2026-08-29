#include "test_harness.h"
#include "../ofxICExample/src/ExampleRuntimePaths.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

class RuntimeFixture {
public:
	RuntimeFixture() {
		root = std::filesystem::temp_directory_path() /
			("ofxic-runtime-paths-" + std::to_string(
				std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(root);
	}

	~RuntimeFixture() {
		std::error_code error;
		std::filesystem::remove_all(root, error);
	}

	std::filesystem::path add(
		const std::string & directory,
		const std::string & executable,
		std::filesystem::file_time_type modified) {
		const auto path = root / directory / executable;
		std::filesystem::create_directories(path.parent_path());
		std::ofstream(path, std::ios::binary) << "fixture";
		std::filesystem::last_write_time(path, modified);
		return path;
	}

	std::filesystem::path root;
};

} // namespace

OFXIC_TEST(example_runtime_paths_prefers_a_valid_configured_executable) {
	RuntimeFixture fixture;
	const auto configured = fixture.add("manual", "llama-server.exe",
		std::filesystem::file_time_type::clock::now());
	fixture.add("llama.cpp-new", "llama-server.exe",
		std::filesystem::file_time_type::clock::now());

	const auto resolved = ofxICExample::resolveInstalledExecutable(
		configured.string(), fixture.root.string(), "llama.cpp-", "llama-server.exe");
	OFXIC_REQUIRE(std::filesystem::equivalent(resolved, configured));
}

OFXIC_TEST(example_runtime_paths_discovers_the_newest_matching_installation) {
	RuntimeFixture fixture;
	const auto now = std::filesystem::file_time_type::clock::now();
	fixture.add("llama.cpp-old", "llama-server.exe", now - std::chrono::hours(2));
	const auto newest = fixture.add(
		"llama.cpp-new/bin", "llama-server.exe", now - std::chrono::hours(1));
	fixture.add("stable-diffusion.cpp-new", "llama-server.exe", now);

	const auto resolved = ofxICExample::findInstalledExecutable(
		fixture.root.string(), "llama.cpp-", "llama-server.exe");
	OFXIC_REQUIRE(std::filesystem::equivalent(resolved, newest));
}

OFXIC_TEST(example_runtime_paths_returns_empty_for_missing_family) {
	RuntimeFixture fixture;
	fixture.add("other-runtime", "llama-server.exe",
		std::filesystem::file_time_type::clock::now());
	OFXIC_REQUIRE(ofxICExample::findInstalledExecutable(
		fixture.root.string(), "llama.cpp-", "llama-server.exe").empty());
}
