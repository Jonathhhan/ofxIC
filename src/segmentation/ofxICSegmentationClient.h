#pragma once

#include "ofxICSegmentationTypes.h"
#include "../endpoint/ofxICEndpoint.h"

#include <functional>

namespace ofxIC {

class SegmentationClient {
public:
	explicit SegmentationClient(Endpoint & endpoint);
	SegmentationBridgeStatus inspectSamBridge(
		RequestControl control = {}) const;
	SegmentationBridgeStatus inspectSamBridge(
		std::function<bool()> shouldCancel) const;
	SegmentationResult segmentSamBridge(
		const SegmentationRequest & request,
		RequestControl control = {}) const;
	SegmentationResult segmentSamBridge(
		const SegmentationRequest & request,
		std::function<bool()> shouldCancel) const;

private:
	Endpoint & endpoint;
};

} // namespace ofxIC
