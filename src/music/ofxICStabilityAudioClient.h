#pragma once

#include "ofxICStabilityAudioTypes.h"

namespace ofxIC {

class Endpoint;

class StabilityAudioClient {
public:
	explicit StabilityAudioClient(Endpoint & endpoint);

	StabilityAudioJob submit(const StabilityAudioRequest & request,
		RequestControl control = {}) const;
	StabilityAudioJob poll(const StabilityAudioJob & job,
		RequestControl control = {}) const;

private:
	Endpoint & endpoint;
};

} // namespace ofxIC
