#pragma once

#include "ofxICStabilityAudioTypes.h"

namespace ofxIC {

class Endpoint;

class StabilityAudioClient {
public:
	explicit StabilityAudioClient(Endpoint & endpoint);

	StabilityAudioJob submit(const StabilityAudioRequest & request) const;
	StabilityAudioJob poll(const StabilityAudioJob & job) const;

private:
	Endpoint & endpoint;
};

} // namespace ofxIC
