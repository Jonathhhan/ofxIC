#pragma once

#include "../endpoint/ofxICRequestTypes.h"

#include <string>
#include <vector>

namespace ofxIC {

enum class MediaKind {
	Image,
	Video
};

enum class MediaProtocol {
	StableDiffusionCpp,
	HuggingFaceFal
};

enum class MediaJobState {
	Unknown,
	Queued,
	Generating,
	Completed,
	Failed,
	Cancelled
};

struct MediaCapabilities {
	bool success = false;
	bool cancelled = false;
	RequestFailure failure = RequestFailure::None;
	int httpStatus = 0;
	std::string model;
	std::string currentMode;
	std::vector<std::string> supportedModes;
	std::vector<std::string> imageOutputFormats;
	std::vector<std::string> videoOutputFormats;
	std::vector<std::string> samplers;
	std::vector<std::string> schedulers;
	int minWidth = 0;
	int maxWidth = 0;
	int minHeight = 0;
	int maxHeight = 0;
	int defaultWidth = 0;
	int defaultHeight = 0;
	int defaultVideoFrames = 0;
	int defaultFps = 0;
	std::string defaultOutputFormat;
	std::string error;
	std::string rawResponse;

	bool supports(MediaKind kind) const {
		const std::string required = kind == MediaKind::Video ? "vid_gen" : "img_gen";
		for (const auto & mode : supportedModes) if (mode == required) return true;
		return false;
	}

	explicit operator bool() const { return success; }
};

struct ImageRequest {
	std::string prompt;
	std::string model;
	int width = 1024;
	int height = 1024;
	int count = 1;
	std::string outputFormat = "png";
};

struct ImageResult {
	bool success = false;
	bool cancelled = false;
	RequestFailure failure = RequestFailure::None;
	int httpStatus = 0;
	std::string outputFormat;
	std::vector<std::string> imagesBase64;
	std::string imageBytes;
	std::vector<std::string> urls;
	std::string error;
	std::string rawResponse;

	explicit operator bool() const {
		return success;
	}
};

struct MediaJobRequest {
	MediaKind kind = MediaKind::Image;
	std::string prompt;
	std::string model;
	std::string negativePrompt;
	int width = 1024;
	int height = 1024;
	int seed = -1;
	int steps = 28;
	float guidance = 7.0f;
	std::string sampleMethod;
	std::string scheduler;
	int imageCount = 1;
	int videoFrames = 33;
	int fps = 16;
	std::string outputFormat;
};

struct MediaJob {
	bool success = false;
	bool cancelled = false;
	RequestFailure failure = RequestFailure::None;
	int httpStatus = 0;
	MediaKind kind = MediaKind::Image;
	MediaProtocol protocol = MediaProtocol::StableDiffusionCpp;
	MediaJobState state = MediaJobState::Unknown;
	std::string id;
	std::string pollUrl;
	std::string resultUrl;
	std::string outputFormat;
	std::string mimeType;
	int fps = 0;
	int frameCount = 0;
	std::vector<std::string> payloadsBase64;
	std::vector<std::string> payloadBytes;
	std::string error;
	std::string rawResponse;

	bool terminal() const {
		return state == MediaJobState::Completed ||
			state == MediaJobState::Failed ||
			state == MediaJobState::Cancelled;
	}

	explicit operator bool() const {
		return success;
	}
};

} // namespace ofxIC
