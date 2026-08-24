#include "test_harness.h"
#include "../ofxICExample/src/ExampleSettings.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

namespace {

const std::string settingsPath = "ofxic-example-settings-fixture.cfg";

} // namespace

OFXIC_TEST(example_settings_round_trip_non_secret_values) {
	ofxICExample::ExampleSettings saved;
	saved.endpointProfile = 4;
	saved.endpointUrl = "https://example.test/v1?mode=custom";
	saved.modelId = "org/model with spaces";
	saved.mediaBackend = 1;
	saved.mediaKind = 1;
	saved.mediaEndpointUrl = "https://media.example.test";
	saved.mediaImageModel = "org/image-model";
	saved.mediaVideoModel = "org/video-model";
	saved.mediaWidth = 768;
	saved.mediaHeight = 432;
	saved.mediaFrames = 49;
	saved.mediaFps = 24;

	OFXIC_REQUIRE(ofxICExample::saveSettings(settingsPath, saved));
	ofxICExample::ExampleSettings loaded;
	OFXIC_REQUIRE(
		ofxICExample::loadSettings(settingsPath, loaded) ==
		ofxICExample::SettingsLoadStatus::Loaded);
	OFXIC_REQUIRE(loaded.endpointProfile == saved.endpointProfile);
	OFXIC_REQUIRE(loaded.endpointUrl == saved.endpointUrl);
	OFXIC_REQUIRE(loaded.modelId == saved.modelId);
	OFXIC_REQUIRE(loaded.mediaBackend == saved.mediaBackend);
	OFXIC_REQUIRE(loaded.mediaKind == saved.mediaKind);
	OFXIC_REQUIRE(loaded.mediaEndpointUrl == saved.mediaEndpointUrl);
	OFXIC_REQUIRE(loaded.mediaImageModel == saved.mediaImageModel);
	OFXIC_REQUIRE(loaded.mediaVideoModel == saved.mediaVideoModel);
	OFXIC_REQUIRE(loaded.mediaWidth == saved.mediaWidth);
	OFXIC_REQUIRE(loaded.mediaHeight == saved.mediaHeight);
	OFXIC_REQUIRE(loaded.mediaFrames == saved.mediaFrames);
	OFXIC_REQUIRE(loaded.mediaFps == saved.mediaFps);

	std::ifstream input(settingsPath, std::ios::binary);
	const std::string serialized(
		(std::istreambuf_iterator<char>(input)),
		std::istreambuf_iterator<char>());
	OFXIC_REQUIRE(serialized.find("token") == std::string::npos);
	OFXIC_REQUIRE(serialized.find("api_key") == std::string::npos);
	input.close();
	OFXIC_REQUIRE(ofxICExample::removeSettings(settingsPath));
}

OFXIC_TEST(example_settings_reject_corruption_without_changing_defaults) {
	{
		std::ofstream output(settingsPath, std::ios::binary | std::ios::trunc);
		output << "version=1\nmedia_fps=0\n";
	}
	ofxICExample::ExampleSettings settings;
	settings.modelId = "safe-default";
	OFXIC_REQUIRE(
		ofxICExample::loadSettings(settingsPath, settings) ==
		ofxICExample::SettingsLoadStatus::Invalid);
	OFXIC_REQUIRE(settings.modelId == "safe-default");
	OFXIC_REQUIRE(settings.mediaFps == 16);
	OFXIC_REQUIRE(ofxICExample::removeSettings(settingsPath));
	OFXIC_REQUIRE(
		ofxICExample::loadSettings(settingsPath, settings) ==
		ofxICExample::SettingsLoadStatus::Missing);
}

OFXIC_TEST(example_environment_overrides_saved_settings_predictably) {
	ofxICExample::ExampleSettings settings;
	settings.endpointProfile = 1;
	settings.endpointUrl = "http://127.0.0.1:1234";
	settings.modelId = "stored-model";
	settings.mediaBackend = 2;
	settings.mediaEndpointUrl = "http://stored-media.test";
	settings.mediaHeight = 720;

	const std::map<std::string, std::string> environment{
		{ "OFXIC_API_KEY", "must-never-be-persisted" },
		{ "OFXIC_ENDPOINT_URL", "https://api.openai.com/v1" },
		{ "OFXIC_MODEL", "environment-model" },
		{ "OFXIC_MEDIA_BACKEND", "hf" },
		{ "OFXIC_MEDIA_ENDPOINT_URL", "https://media.environment.test" },
		{ "OFXIC_MEDIA_VIDEO_MODEL", "environment-video" },
		{ "OFXIC_MEDIA_KIND", "video" },
		{ "OFXIC_MEDIA_WIDTH", "1024" },
		{ "OFXIC_MEDIA_HEIGHT", "invalid" },
		{ "OFXIC_MEDIA_FRAMES", "65" },
		{ "OFXIC_MEDIA_FPS", "30" },
	};
	ofxICExample::applyEnvironmentOverrides(settings, environment);

	OFXIC_REQUIRE(settings.endpointProfile == 3);
	OFXIC_REQUIRE(settings.endpointUrl == "https://api.openai.com/v1");
	OFXIC_REQUIRE(settings.modelId == "environment-model");
	OFXIC_REQUIRE(settings.mediaBackend == 1);
	OFXIC_REQUIRE(settings.mediaEndpointUrl == "https://media.environment.test");
	OFXIC_REQUIRE(settings.mediaImageModel == "black-forest-labs/FLUX.1-dev");
	OFXIC_REQUIRE(settings.mediaVideoModel == "environment-video");
	OFXIC_REQUIRE(settings.mediaKind == 1);
	OFXIC_REQUIRE(settings.mediaWidth == 1024);
	OFXIC_REQUIRE(settings.mediaHeight == 720);
	OFXIC_REQUIRE(settings.mediaFrames == 65);
	OFXIC_REQUIRE(settings.mediaFps == 30);
	OFXIC_REQUIRE(ofxICExample::saveSettings(settingsPath, settings));
	std::ifstream input(settingsPath, std::ios::binary);
	const std::string serialized(
		(std::istreambuf_iterator<char>(input)),
		std::istreambuf_iterator<char>());
	OFXIC_REQUIRE(serialized.find("must-never-be-persisted") == std::string::npos);
	input.close();
	OFXIC_REQUIRE(ofxICExample::removeSettings(settingsPath));
}
