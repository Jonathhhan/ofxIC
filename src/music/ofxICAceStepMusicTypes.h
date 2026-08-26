#pragma once

#include "../endpoint/ofxICRequestTypes.h"

#include <cstdint>
#include <string>

namespace ofxIC {

enum class AceStepMusicJobState {
	Unknown,
	Submitted,
	Generating,
	Completed,
	Failed
};

enum class AceStepMusicJobPhase {
	LanguageModel,
	Synthesis
};

struct AceStepMusicRequest {
	std::string caption;
	std::string lyrics;
	int bpm = 0;
	int durationSeconds = 30;
	std::string keyScale;
	std::string timeSignature = "4";
	std::int64_t seed = -1;
	std::string negativePrompt;
	bool instrumentalOnly = true;
	std::string outputFormat = "wav";
};

struct AceStepMusicJob {
	bool success = false;
	bool cancelled = false;
	RequestFailure failure = RequestFailure::None;
	int httpStatus = 0;
	AceStepMusicJobState state = AceStepMusicJobState::Unknown;
	AceStepMusicJobPhase phase = AceStepMusicJobPhase::LanguageModel;
	std::string id;
	std::string outputFormat;
	std::string mimeType;
	std::string audioBytes;
	std::string error;
	std::string rawResponse;

	bool terminal() const {
		return state == AceStepMusicJobState::Completed ||
			state == AceStepMusicJobState::Failed;
	}

	explicit operator bool() const {
		return success;
	}
};

} // namespace ofxIC
