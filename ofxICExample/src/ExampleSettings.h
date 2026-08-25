#pragma once

#include <map>
#include <string>

namespace ofxICExample {

struct ExampleSettings {
	int endpointProfile = 0;
	std::string endpointUrl = "http://127.0.0.1:8080";
	std::string modelId;
	std::string transcriptionEndpointUrl = "http://127.0.0.1:8080";
	int transcriptionProtocol = 0;
	std::string transcriptionModel = "whisper-1";
	std::string segmentationEndpointUrl = "http://127.0.0.1:18085";
	int mediaBackend = 2;
	int mediaKind = 0;
	std::string mediaEndpointUrl = "http://127.0.0.1:8080";
	std::string mediaImageModel = "black-forest-labs/FLUX.1-dev";
	std::string mediaVideoModel = "Wan-AI/Wan2.2-TI2V-5B";
	int mediaWidth = 512;
	int mediaHeight = 512;
	int mediaFrames = 33;
	int mediaFps = 16;
};

enum class SettingsLoadStatus {
	Missing,
	Loaded,
	Invalid,
};

SettingsLoadStatus loadSettings(const std::string & path, ExampleSettings & settings);
bool saveSettings(const std::string & path, const ExampleSettings & settings);
bool removeSettings(const std::string & path);

void applyEnvironmentOverrides(
	ExampleSettings & settings,
	const std::map<std::string, std::string> & environment);

} // namespace ofxICExample
