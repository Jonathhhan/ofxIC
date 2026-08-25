#include "ofxICSegmentationClient.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace ofxIC {
namespace {

std::string safeFilename(std::string value) {
	value.erase(std::remove_if(value.begin(), value.end(), [](char c) {
		return c == '\r' || c == '\n' || c == '"' || c == '\\';
	}), value.end());
	return value.empty() ? "image.ppm" : value;
}

void appendField(std::string & body, const std::string & boundary,
	const std::string & name, const std::string & value) {
	body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" +
		name + "\"\r\n\r\n" + value + "\r\n";
}

} // namespace

SegmentationClient::SegmentationClient(Endpoint & endpoint) : endpoint(endpoint) {}

SegmentationResult SegmentationClient::segmentSamBridge(
	const SegmentationRequest & request,
	std::function<bool()> shouldCancel) const {
	SegmentationResult result;
	if (request.imageBytes.empty()) {
		result.error = "segmentation image is empty";
		return result;
	}
	if (request.points.empty()) {
		result.error = "segmentation requires at least one point";
		return result;
	}
	for (const auto & point : request.points) {
		if (point.x < 0.0f || point.x > 1.0f || point.y < 0.0f || point.y > 1.0f) {
			result.error = "segmentation point must use normalized coordinates";
			return result;
		}
	}

	std::string boundary = "----ofxICSamBridgeBoundary";
	while (request.imageBytes.find(boundary) != std::string::npos) boundary += "x";
	std::string body;
	body.reserve(request.imageBytes.size() + request.points.size() * 256U + 512U);
	body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"image\"; filename=\"" +
		safeFilename(request.filename) + "\"\r\nContent-Type: image/x-portable-pixmap\r\n\r\n";
	body.append(request.imageBytes);
	body += "\r\n";
	for (const auto & point : request.points) {
		std::ostringstream value;
		value << point.x << ',' << point.y << ',' << (point.positive ? "positive" : "negative");
		appendField(body, boundary, "point", value.str());
	}
	body += "--" + boundary + "--\r\n";

	HttpRequest httpRequest;
	httpRequest.method = HttpMethod::Post;
	httpRequest.url = "/v1/segmentations";
	httpRequest.body = std::move(body);
	httpRequest.contentType = "multipart/form-data; boundary=" + boundary;
	httpRequest.accept = "image/x-portable-graymap";
	httpRequest.timeoutSeconds = 300;
	httpRequest.maxResponseBytes = 64U * 1024U * 1024U;
	httpRequest.shouldCancel = std::move(shouldCancel);
	const auto response = endpoint.perform(std::move(httpRequest));
	result.httpStatus = response.status;
	result.cancelled = response.cancelled;
	if (response.cancelled) {
		result.error = response.error.empty() ? "request cancelled" : response.error;
		return result;
	}
	if (!response.started || response.status < 200 || response.status >= 300) {
		result.error = response.error.empty()
			? "SAM bridge returned HTTP " + std::to_string(response.status)
			: response.error;
		return result;
	}
	if (response.body.size() < 3 || response.body[0] != 'P' ||
		(response.body[1] != '5' && response.body[1] != '2')) {
		result.error = "SAM bridge returned no PGM mask";
		return result;
	}
	result.maskBytes = response.body;
	result.success = true;
	return result;
}

} // namespace ofxIC
