#include "test_harness.h"
#include "../ofxICExample/src/ExampleJobHistory.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {

std::filesystem::path historyFixturePath() {
	return std::filesystem::temp_directory_path() /
		("ofxic-history-" + std::to_string(
			std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt");
}

} // namespace

OFXIC_TEST(example_job_history_round_trips_privacy_aware_metadata) {
	const auto path = historyFixturePath();
	ofxICExample::JobHistory saved(10);
	saved.add({ "2026-08-28 12:00:00", "image", "completed",
		"Generated local image", "C:/outputs/image.png" });
	OFXIC_REQUIRE(saved.save(path.string()));

	ofxICExample::JobHistory loaded(10);
	OFXIC_REQUIRE(loaded.load(path.string()));
	OFXIC_REQUIRE(loaded.entries().size() == 1);
	OFXIC_REQUIRE(loaded.entries().front().task == "image");
	OFXIC_REQUIRE(loaded.entries().front().outputPath == "C:/outputs/image.png");
	std::ifstream input(path, std::ios::binary);
	const std::string serialized((std::istreambuf_iterator<char>(input)),
		std::istreambuf_iterator<char>());
	OFXIC_REQUIRE(serialized.find("prompt") == std::string::npos);
	OFXIC_REQUIRE(serialized.find("api_key") == std::string::npos);
	input.close();
	OFXIC_REQUIRE(loaded.clear(path.string()));
}

OFXIC_TEST(example_job_history_keeps_only_the_configured_recent_entries) {
	ofxICExample::JobHistory history(2);
	history.add({ "1", "chat", "completed", "first", "" });
	history.add({ "2", "image", "completed", "second", "two.png" });
	history.add({ "3", "music", "failed", "third", "" });
	OFXIC_REQUIRE(history.entries().size() == 2);
	OFXIC_REQUIRE(history.entries().front().timestamp == "2");
	OFXIC_REQUIRE(history.entries().back().timestamp == "3");
}

OFXIC_TEST(example_job_history_rejects_multiline_and_embedded_null_fields) {
	ofxICExample::JobHistory history(4);
	history.add({ "1", "chat", "completed", "line one\nline two", "" });
	OFXIC_REQUIRE(history.entries().empty());
	std::string embeddedNull("status\0secret", 13);
	history.add({ "2", "chat", "completed", embeddedNull, "" });
	OFXIC_REQUIRE(history.entries().empty());
}
