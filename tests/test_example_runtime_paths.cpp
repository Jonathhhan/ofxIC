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

OFXIC_TEST(example_runtime_paths_resolve_relative_paths_from_workbench_root) {
	RuntimeFixture fixture;
	const auto workbench = fixture.root / "bin";
	const auto expected = workbench / "runtime/servers/llama/llama-server.exe";
	const std::string resolved = ofxICExample::resolveWorkbenchPath(
		"runtime/servers/llama/llama-server.exe", workbench.string());
	OFXIC_REQUIRE(std::filesystem::path(resolved) == expected.lexically_normal());
}

OFXIC_TEST(example_runtime_paths_store_paths_inside_workbench_as_portable) {
	RuntimeFixture fixture;
	const auto workbench = fixture.root / "bin";
	const auto server = workbench / "runtime/servers/sd/sd-server.exe";
	const std::string portable = ofxICExample::portableWorkbenchPath(
		server.string(), workbench.string());
	OFXIC_REQUIRE(portable == "runtime/servers/sd/sd-server.exe");
}

OFXIC_TEST(example_runtime_paths_keep_external_models_absolute) {
	RuntimeFixture fixture;
	const auto workbench = fixture.root / "bin";
	const auto model = fixture.root / "models/model.gguf";
	const std::string portable = ofxICExample::portableWorkbenchPath(
		model.string(), workbench.string());
	OFXIC_REQUIRE(std::filesystem::path(portable) == model.lexically_normal());
	OFXIC_REQUIRE(std::filesystem::path(portable).is_absolute());
}

OFXIC_TEST(example_runtime_paths_do_not_store_parent_traversal_as_portable) {
	RuntimeFixture fixture;
	const auto workbench = fixture.root / "bin";
	const std::string portable = ofxICExample::portableWorkbenchPath(
		"../models/model.gguf", workbench.string());
	OFXIC_REQUIRE(std::filesystem::path(portable).is_absolute());
	OFXIC_REQUIRE(std::filesystem::path(portable) ==
		(fixture.root / "models/model.gguf").lexically_normal());
}

OFXIC_TEST(example_runtime_paths_portable_round_trip_preserves_target) {
	RuntimeFixture fixture;
	const auto workbench = fixture.root / "bin";
	const auto model = workbench / "runtime/models/whisper/base.bin";
	const std::string portable = ofxICExample::portableWorkbenchPath(
		model.string(), workbench.string());
	const std::string resolved = ofxICExample::resolveWorkbenchPath(
		portable, workbench.string());
	OFXIC_REQUIRE(std::filesystem::path(resolved) == model.lexically_normal());
}

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

OFXIC_TEST(example_runtime_paths_prefers_installed_root_over_build_artifacts) {
	RuntimeFixture fixture;
	const auto now = std::filesystem::file_time_type::clock::now();
	const auto installed = fixture.add(
		"acestep.cpp-release", "ace-server.exe", now - std::chrono::hours(2));
	fixture.add("acestep.cpp-release/build/Release", "ace-server.exe", now);

	const auto resolved = ofxICExample::findInstalledExecutable(
		fixture.root.string(), "acestep.cpp-", "ace-server.exe");
	OFXIC_REQUIRE(std::filesystem::equivalent(resolved, installed));
}

OFXIC_TEST(example_runtime_paths_returns_empty_for_missing_family) {
	RuntimeFixture fixture;
	fixture.add("other-runtime", "llama-server.exe",
		std::filesystem::file_time_type::clock::now());
	OFXIC_REQUIRE(ofxICExample::findInstalledExecutable(
		fixture.root.string(), "llama.cpp-", "llama-server.exe").empty());
}

OFXIC_TEST(example_runtime_paths_reports_search_evidence) {
	RuntimeFixture fixture;
	fixture.add("whisper.cpp-build/bin", "whisper-server.exe",
		std::filesystem::file_time_type::clock::now());
	const std::string diagnostic = ofxICExample::installedExecutableSearchDiagnostic(
		fixture.root.string(), "whisper.cpp-", "whisper-server.exe");
	OFXIC_REQUIRE(diagnostic.find("root_state=directory") != std::string::npos);
	OFXIC_REQUIRE(diagnostic.find("family_directories=1") != std::string::npos);
	OFXIC_REQUIRE(diagnostic.find("matching_files=1") != std::string::npos);
#if defined(_WIN32)
	OFXIC_REQUIRE(diagnostic.find("native_root_state=directory") != std::string::npos);
	OFXIC_REQUIRE(diagnostic.find("native_result=") != std::string::npos);
	OFXIC_REQUIRE(diagnostic.find("whisper-server.exe") != std::string::npos);
#endif
}

OFXIC_TEST(example_runtime_paths_reports_native_evidence_for_missing_root) {
	RuntimeFixture fixture;
	const std::string diagnostic = ofxICExample::installedExecutableSearchDiagnostic(
		(fixture.root / "missing").string(), "llama.cpp-", "llama-server.exe");
	OFXIC_REQUIRE(diagnostic.find("root_state=") != std::string::npos);
#if defined(_WIN32)
	OFXIC_REQUIRE(diagnostic.find("native_root_state=error-") != std::string::npos);
	OFXIC_REQUIRE(diagnostic.find("native_result=none") != std::string::npos);
#endif
}

OFXIC_TEST(example_runtime_paths_uses_startup_snapshot_when_ui_path_is_lost) {
	RuntimeFixture fixture;
	const auto detected = fixture.add("startup", "ace-server.exe",
		std::filesystem::file_time_type::clock::now());

	const auto resolved = ofxICExample::resolveInstalledExecutable(
		"", detected.string(), (fixture.root / "missing-root").string(),
		"acestep.cpp-", "ace-server.exe");
	OFXIC_REQUIRE(std::filesystem::equivalent(resolved, detected));
}

OFXIC_TEST(example_runtime_paths_prefers_configured_path_over_startup_snapshot) {
	RuntimeFixture fixture;
	const auto now = std::filesystem::file_time_type::clock::now();
	const auto configured = fixture.add("manual", "sd-server.exe", now);
	const auto detected = fixture.add("startup", "sd-server.exe", now);

	const auto resolved = ofxICExample::resolveInstalledExecutable(
		configured.string(), detected.string(), fixture.root.string(),
		"stable-diffusion.cpp-", "sd-server.exe");
	OFXIC_REQUIRE(std::filesystem::equivalent(resolved, configured));
}

OFXIC_TEST(example_runtime_paths_discovers_each_sibling_runtime_family) {
	RuntimeFixture fixture;
	const auto now = std::filesystem::file_time_type::clock::now();
	const auto llama = fixture.add(
		"llama.cpp-build", "llama-server.exe", now);
	const auto stableDiffusion = fixture.add(
		"stable-diffusion.cpp-build", "sd-server.exe", now);
	const auto whisper = fixture.add(
		"whisper.cpp-build", "whisper-server.exe", now);
	const auto sam = fixture.add(
		"sam-python-build/.venv/Scripts", "python.exe", now);

	OFXIC_REQUIRE(std::filesystem::equivalent(
		ofxICExample::findInstalledExecutable(fixture.root.string(),
			"llama.cpp-", "llama-server.exe"), llama));
	OFXIC_REQUIRE(std::filesystem::equivalent(
		ofxICExample::findInstalledExecutable(fixture.root.string(),
			"stable-diffusion.cpp-", "sd-server.exe"), stableDiffusion));
	OFXIC_REQUIRE(std::filesystem::equivalent(
		ofxICExample::findInstalledExecutable(fixture.root.string(),
			"whisper.cpp-", "whisper-server.exe"), whisper));
	OFXIC_REQUIRE(std::filesystem::equivalent(
		ofxICExample::findInstalledExecutable(fixture.root.string(),
			"sam-python-", "python.exe"), sam));
}

#if defined(_WIN32)
OFXIC_TEST(example_runtime_paths_match_windows_names_without_case_sensitivity) {
	RuntimeFixture fixture;
	const auto expected = fixture.add("WHISPER.CPP-BUILD", "WHISPER-SERVER.EXE",
		std::filesystem::file_time_type::clock::now());
	const auto resolved = ofxICExample::findInstalledExecutable(
		fixture.root.string(), "whisper.cpp-", "whisper-server.exe");
	OFXIC_REQUIRE(std::filesystem::equivalent(resolved, expected));
}
#endif
