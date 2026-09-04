#pragma once

#include "../../src/media/ofxICMediaTypes.h"

#include <string>

namespace ofxICExample {

struct MediaRuntimeConfig {
	std::string serverPath;
	std::string modelPath;
	std::string vaePath;
	std::string clipLPath;
	std::string clipGPath;
	std::string textEncoderPath;
	bool completeCheckpoint = false;
	bool flashAttention = false;
	bool offloadToCpu = false;
};

struct MediaControlSelection {
	int kind = 0;
	std::string sampler;
	std::string scheduler;
	std::string outputFormat;
};

std::string mediaRuntimeSignature(const MediaRuntimeConfig & config);
bool mediaModelMatches(const std::string & loadedModel, const std::string & selectedPath);
void reconcileMediaControls(const ofxIC::MediaCapabilities & capabilities,
	bool contextMatchesSelection, MediaControlSelection & selection);
void applySafeMediaDefaults(const ofxIC::MediaCapabilities & capabilities,
	int & width, int & height, int & frames, int & fps);

} // namespace ofxICExample
