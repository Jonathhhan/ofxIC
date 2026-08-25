#pragma once

#include "ofxICAceStepMusicTypes.h"

namespace ofxIC {

class Endpoint;

class AceStepMusicClient {
public:
	explicit AceStepMusicClient(Endpoint & endpoint);

	AceStepMusicJob submit(const AceStepMusicRequest & request) const;
	AceStepMusicJob poll(const AceStepMusicJob & job) const;

private:
	AceStepMusicJob submitSynthesis(
		const std::string & languageModelResponse,
		const std::string & outputFormat) const;
	Endpoint & endpoint;
};

} // namespace ofxIC
