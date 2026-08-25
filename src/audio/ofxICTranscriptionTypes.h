#pragma once

#include <string>

namespace ofxIC {

struct TranscriptionRequest {
	std::string audioBytes;
	std::string filename = "audio.wav";
	std::string contentType = "audio/wav";
	std::string model = "whisper-1";
	std::string language;
	std::string prompt;
};

struct TranscriptionResult {
	bool success = false;
	bool cancelled = false;
	int httpStatus = 0;
	std::string text;
	std::string rawResponse;
	std::string error;
	explicit operator bool() const { return success; }
};

} // namespace ofxIC
