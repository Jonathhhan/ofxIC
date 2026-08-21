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
	MediaJob poll(const MediaJob & job) const;
	MediaJob poll(const std::string & idOrPollUrl) const;
	MediaJob cancel(const MediaJob & job) const;

private:
	static std::string buildImageBody(const ImageRequest & request);
	static std::string buildJobBody(const MediaJobRequest & request);
	static MediaJob parseJob(
		const HttpResponse & response,
		MediaKind fallbackKind,
		std::string fallbackPollUrl = {});

	Endpoint & endpoint;
};

} // namespace ofxIC
