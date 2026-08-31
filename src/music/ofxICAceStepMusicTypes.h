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

enum class AceStepMusicProtocol {
	Official15,
	NativeCpp
};

enum class AceStepMusicJobPhase {
	LanguageModel,
	Synthesis
};

struct AceStepMusicRequest {
	std::string caption;
	std::string lyrics;
	std::string model = "acestep-v15-turbo";
	// Exact filename from acestep.cpp /props models.dit. Empty lets the
	// native server select its first registered DiT model.
	std::string nativeSynthModel;
	std::string vocalLanguage = "en";
	int bpm = 0;
	int durationSeconds = 30;
	int inferenceSteps = 8;
	std::string keyScale;
	std::string timeSignature = "4";
	std::int64_t seed = -1;
	std::string negativePrompt;
	bool instrumentalOnly = true;
	bool thinking = false;
	bool useFormat = false;
	std::string outputFormat = "wav";
	AceStepMusicProtocol protocol = AceStepMusicProtocol::Official15;
};

struct AceStepMusicJob {
	bool success = false;
	bool cancelled = false;
	RequestFailure failure = RequestFailure::None;
	int httpStatus = 0;
	AceStepMusicJobState state = AceStepMusicJobState::Unknown;
	AceStepMusicJobPhase phase = AceStepMusicJobPhase::Synthesis;
	AceStepMusicProtocol protocol = AceStepMusicProtocol::Official15;
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
