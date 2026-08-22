#pragma once

#include "ofxICMediaTypes.h"
#include "../endpoint/ofxICEndpoint.h"

#include <string>

namespace ofxIC {

class MediaClient {
public:
	explicit MediaClient(Endpoint & endpoint);

	ImageResult generateImage(const ImageRequest & request) const;
	MediaJob submit(const MediaJobRequest & request) const;
	MediaJob submitHuggingFaceFal(const MediaJobRequest & request) const;
	MediaJob poll(const MediaJob & job) const;
	MediaJob poll(const std::string & idOrPollUrl) const;
	MediaJob cancel(const MediaJob & job) const;

private:
	static std::string buildImageBody(const ImageRequest & request);
	static std::string buildJobBody(const MediaJobRequest & request);
	static std::string buildHuggingFaceFalBody(const MediaJobRequest & request);
	static MediaJob parseJob(
		const HttpResponse & response,
		MediaKind fallbackKind,
		std::string fallbackPollUrl = {});
	MediaJob pollHuggingFaceFal(const MediaJob & job) const;
	bool downloadHuggingFaceFalOutput(const std::string & url, MediaJob & job) const;

	Endpoint & endpoint;
};

} // namespace ofxIC
