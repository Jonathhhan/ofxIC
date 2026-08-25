#pragma once

#include "ofxICTranscriptionTypes.h"
#include "../endpoint/ofxICEndpoint.h"

#include <functional>

namespace ofxIC {

class TranscriptionClient {
public:
	explicit TranscriptionClient(Endpoint & endpoint);
	TranscriptionResult transcribeOpenAI(const TranscriptionRequest & request,
		std::function<bool()> shouldCancel = nullptr) const;
	TranscriptionResult transcribeWhisperCpp(const TranscriptionRequest & request,
		std::function<bool()> shouldCancel = nullptr) const;

private:
	TranscriptionResult transcribe(
		const TranscriptionRequest & request,
		const std::string & path,
		bool includeModel,
		std::function<bool()> shouldCancel) const;
	Endpoint & endpoint;
};

} // namespace ofxIC
