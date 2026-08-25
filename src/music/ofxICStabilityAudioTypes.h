#pragma once

#include <cstdint>
#include <string>

namespace ofxIC {

enum class StabilityAudioJobState {
	Unknown,
	Submitted,
	Generating,
	Completed,
	Failed
};

struct StabilityAudioRequest {
	std::string prompt;
	std::string model = "stable-audio-3";
	int durationSeconds = 30;
	std::uint32_t seed = 0;
	int steps = 8;
	float guidance = 1.0f;
	std::string outputFormat = "mp3";
};

struct StabilityAudioJob {
	bool success = false;
	int httpStatus = 0;
	StabilityAudioJobState state = StabilityAudioJobState::Unknown;
	std::string id;
	std::string outputFormat;
	std::string mimeType;
	std::string audioBytes;
	std::string error;
	std::string rawResponse;

	bool terminal() const {
		return state == StabilityAudioJobState::Completed ||
			state == StabilityAudioJobState::Failed;
	}

	explicit operator bool() const {
		return success;
	}
};

} // namespace ofxIC
