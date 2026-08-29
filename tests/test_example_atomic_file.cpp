#include "test_harness.h"
#include "../ofxICExample/src/ExampleAtomicFile.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

const std::filesystem::path atomicRoot = "ofxic-example-atomic-file-fixture";

std::string readFile(const std::filesystem::path & path) {
	std::ifstream input(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>());
}

bool hasTemporarySibling(const std::filesystem::path & target) {
	const std::string prefix = target.filename().string() + ".tmp.";
	std::error_code error;
	for (const auto & entry : std::filesystem::directory_iterator(
		target.parent_path(), error)) {
		if (entry.path().filename().string().starts_with(prefix)) return true;
	}
	return false;
}

void removeAtomicFixture() {
	std::error_code ignored;
	std::filesystem::remove_all(atomicRoot, ignored);
}

} // namespace

OFXIC_TEST(example_atomic_file_creates_parents_and_replaces_complete_content) {
	removeAtomicFixture();
	const std::filesystem::path target = atomicRoot / "nested" / "settings.txt";
	std::string error;
	OFXIC_REQUIRE(ofxICExample::writeTextFileAtomically(
		target.string(), "first-complete-value", &error));
	OFXIC_REQUIRE(error.empty());
	OFXIC_REQUIRE(readFile(target) == "first-complete-value");
	OFXIC_REQUIRE(ofxICExample::writeTextFileAtomically(
		target.string(), "second-complete-value", &error));
	OFXIC_REQUIRE(readFile(target) == "second-complete-value");
	OFXIC_REQUIRE(!hasTemporarySibling(target));
	removeAtomicFixture();
}

OFXIC_TEST(example_atomic_file_preserves_destination_on_replace_failure) {
	removeAtomicFixture();
	const std::filesystem::path target = atomicRoot / "existing-directory";
	std::filesystem::create_directories(target);
	{
		std::ofstream sentinel(target / "keep.txt", std::ios::binary);
		sentinel << "preserve-me";
	}
	std::string error;
	OFXIC_REQUIRE(!ofxICExample::writeTextFileAtomically(
		target.string(), "must-not-replace-directory", &error));
	OFXIC_REQUIRE(!error.empty());
	OFXIC_REQUIRE(std::filesystem::is_directory(target));
	OFXIC_REQUIRE(readFile(target / "keep.txt") == "preserve-me");
	OFXIC_REQUIRE(!hasTemporarySibling(target));
	removeAtomicFixture();
}

OFXIC_TEST(example_atomic_file_rejects_an_empty_destination) {
	std::string error;
	OFXIC_REQUIRE(!ofxICExample::writeTextFileAtomically("", "value", &error));
	OFXIC_REQUIRE(error == "The destination path is empty.");
}
