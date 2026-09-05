#pragma once

#include "ofxICMediaTypes.h"
#include "../endpoint/ofxICEndpoint.h"

#include <string>

namespace ofxIC {

class MediaClient {
public:
	explicit MediaClient(Endpoint & endpoint);

	ImageResult generateImage(const ImageRequest & request, RequestControl control = {}) const;
	// Downloads one returned image URL without forwarding endpoint credentials.
	ImageResult downloadImage(const std::string & url, RequestControl control = {}) const;
	MediaCapabilities inspectCapabilities(RequestControl control = {}) const;
	MediaJob submit(const MediaJobRequest & request, RequestControl control = {}) const;
	MediaJob submitHuggingFaceFal(const MediaJobRequest & request, RequestControl control = {}) const;
	MediaJob poll(const MediaJob & job, RequestControl control = {}) const;
	MediaJob poll(const std::string & idOrPollUrl, RequestControl control = {}) const;
	MediaJob cancel(const MediaJob & job, RequestControl control = {}) const;

private:
	static std::string buildImageBody(const ImageRequest & request);
	static std::string buildJobBody(const MediaJobRequest & request);
	static std::string buildHuggingFaceFalBody(const MediaJobRequest & request);
	static MediaJob parseJob(
		const HttpResponse & response,
		MediaKind fallbackKind,
		std::string fallbackPollUrl = {});
	MediaJob pollHuggingFaceFal(const MediaJob & job, RequestControl control) const;
	bool downloadHuggingFaceFalOutput(const std::string & url, MediaJob & job,
		RequestControl control) const;

	Endpoint & endpoint;
};

} // namespace ofxIC
