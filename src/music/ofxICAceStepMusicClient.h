#pragma once

#include "ofxICAceStepMusicTypes.h"

namespace ofxIC {

class Endpoint;

class AceStepMusicClient {
public:
	explicit AceStepMusicClient(Endpoint & endpoint);

	AceStepMusicJob submit(const AceStepMusicRequest & request,
		RequestControl control = {}) const;
	AceStepMusicJob poll(const AceStepMusicJob & job,
		RequestControl control = {}) const;

private:
	AceStepMusicJob submitSynthesis(
		const std::string & languageModelResponse,
		const std::string & outputFormat,
		RequestControl control) const;
	Endpoint & endpoint;
};

} // namespace ofxIC
