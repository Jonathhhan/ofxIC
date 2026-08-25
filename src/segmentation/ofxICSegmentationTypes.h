#pragma once

#include <string>
#include <vector>

namespace ofxIC {

struct SegmentationPoint {
	float x = 0.5f;
	float y = 0.5f;
	bool positive = true;
};

struct SegmentationRequest {
	std::string imageBytes;
	std::string filename = "image.ppm";
	std::vector<SegmentationPoint> points;
};

struct SegmentationResult {
	bool success = false;
	bool cancelled = false;
	int httpStatus = 0;
	std::string maskBytes;
	std::string error;
	explicit operator bool() const { return success; }
};

} // namespace ofxIC
