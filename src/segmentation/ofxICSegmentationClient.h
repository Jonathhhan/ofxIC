#pragma once

#include "ofxICSegmentationTypes.h"
#include "../endpoint/ofxICEndpoint.h"

#include <functional>

namespace ofxIC {

class SegmentationClient {
public:
	explicit SegmentationClient(Endpoint & endpoint);
	SegmentationBridgeStatus inspectSamBridge(
		std::function<bool()> shouldCancel = nullptr) const;
	SegmentationResult segmentSamBridge(
		const SegmentationRequest & request,
		std::function<bool()> shouldCancel = nullptr) const;

private:
	Endpoint & endpoint;
};

} // namespace ofxIC
