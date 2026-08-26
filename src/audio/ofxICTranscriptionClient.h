#pragma once

#include "ofxICTranscriptionTypes.h"
#include "../endpoint/ofxICEndpoint.h"

#include <functional>

namespace ofxIC {

class TranscriptionClient {
public:
	explicit TranscriptionClient(Endpoint & endpoint);
	TranscriptionResult transcribeOpenAI(const TranscriptionRequest & request,
		RequestControl control = {}) const;
	TranscriptionResult transcribeOpenAI(const TranscriptionRequest & request,
		std::function<bool()> shouldCancel) const;
	TranscriptionResult transcribeWhisperCpp(const TranscriptionRequest & request,
		RequestControl control = {}) const;
	TranscriptionResult transcribeWhisperCpp(const TranscriptionRequest & request,
		std::function<bool()> shouldCancel) const;

private:
	TranscriptionResult transcribe(
		const TranscriptionRequest & request,
		const std::string & path,
		bool includeModel,
		RequestControl control) const;
	Endpoint & endpoint;
};

} // namespace ofxIC
