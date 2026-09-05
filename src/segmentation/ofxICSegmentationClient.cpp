#include "ofxICSegmentationClient.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <locale>
#include <sstream>
#include <utility>

namespace ofxIC {
namespace {

constexpr std::size_t maximumImageBytes = 60U * 1024U * 1024U;
constexpr std::size_t maximumPoints = 64U;

std::string extractStringField(const std::string & json, const std::string & key) {
	const std::string quotedKey = "\"" + key + "\"";
	const std::size_t keyPosition = json.find(quotedKey);
	if (keyPosition == std::string::npos) return {};
	std::size_t position = json.find(':', keyPosition + quotedKey.size());
	if (position == std::string::npos) return {};
	++position;
	while (position < json.size() && std::isspace(
		static_cast<unsigned char>(json[position]))) ++position;
	if (position >= json.size() || json[position++] != '"') return {};
	std::string value;
	bool escaped = false;
	for (; position < json.size(); ++position) {
		const char character = json[position];
		if (escaped) {
			value.push_back(character);
			escaped = false;
		} else if (character == '\\') {
			escaped = true;
		} else if (character == '"') {
			return value;
		} else {
			value.push_back(character);
		}
	}
	return {};
}

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

SegmentationBridgeStatus SegmentationClient::inspectSamBridge(
	std::function<bool()> shouldCancel) const {
	RequestControl control;
	control.shouldCancel = std::move(shouldCancel);
	return inspectSamBridge(std::move(control));
}

SegmentationBridgeStatus SegmentationClient::inspectSamBridge(
	RequestControl control) const {
	SegmentationBridgeStatus result;
	if (control.timeoutSeconds < 0) {
		result.failure = RequestFailure::Validation;
		result.error = "request timeout cannot be negative";
		return result;
	}
	HttpRequest request;
	request.method = HttpMethod::Get;
	request.url = "/health";
	request.accept = "application/json";
	request.headers.emplace_back("X-ofxIC-SAM-Bridge-Version", "1");
	request.timeoutSeconds = control.timeoutSeconds > 0 ? control.timeoutSeconds : 10;
	request.maxResponseBytes = 16U * 1024U;
	request.shouldCancel = std::move(control.shouldCancel);
	const HttpResponse response = endpoint.perform(std::move(request));
	result.httpStatus = response.status;
	result.cancelled = response.cancelled;
	result.failure = response.cancelled ? RequestFailure::Cancelled : response.failure;
	if (response.cancelled) {
		result.error = response.error.empty() ? "request cancelled" : response.error;
		return result;
	}
	if (!response.started || result.failure != RequestFailure::None || response.status < 200 || response.status >= 300) {
		if (result.failure == RequestFailure::None) result.failure = response.status > 0
			? RequestFailure::Provider : RequestFailure::Transport;
		result.error = !response.body.empty() && response.body.size() <= 4096U
			? response.body
			: (response.error.empty() ? "SAM bridge health request failed" : response.error);
		return result;
	}
	const std::string status = extractStringField(response.body, "status");
	result.version = extractStringField(response.body, "version");
	result.mode = extractStringField(response.body, "mode");
	result.backend = extractStringField(response.body, "backend");
	if (status != "ok" || result.version != "1") {
		result.failure = RequestFailure::InvalidResponse;
		result.error = "SAM bridge returned an incompatible health response";
		return result;
	}
	result.reachable = true;
	return result;
}

SegmentationResult SegmentationClient::segmentSamBridge(
	const SegmentationRequest & request,
	std::function<bool()> shouldCancel) const {
	RequestControl control;
	control.shouldCancel = std::move(shouldCancel);
	return segmentSamBridge(request, std::move(control));
}

SegmentationResult SegmentationClient::segmentSamBridge(
	const SegmentationRequest & request,
	RequestControl control) const {
	SegmentationResult result;
	if (control.timeoutSeconds < 0) {
		result.failure = RequestFailure::Validation;
		result.error = "request timeout cannot be negative";
		return result;
	}
	if (request.imageBytes.empty()) {
		result.failure = RequestFailure::Validation;
		result.error = "segmentation image is empty";
		return result;
	}
	if (request.imageBytes.size() > maximumImageBytes) {
		result.failure = RequestFailure::Validation;
		result.error = "segmentation image exceeds the 60 MiB client limit";
		return result;
	}
	if (request.points.empty()) {
		result.failure = RequestFailure::Validation;
		result.error = "segmentation requires at least one point";
		return result;
	}
	if (request.points.size() > maximumPoints) {
		result.failure = RequestFailure::InvalidResponse;
		result.error = "segmentation accepts at most 64 points";
		return result;
	}
	for (const auto & point : request.points) {
		if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
			result.failure = RequestFailure::Validation;
			result.error = "segmentation coordinates must be finite numbers";
			return result;
		}
		if (point.x < 0.0f || point.x > 1.0f || point.y < 0.0f || point.y > 1.0f) {
			result.failure = RequestFailure::Validation;
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
		value.imbue(std::locale::classic());
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
	httpRequest.headers.emplace_back("X-ofxIC-SAM-Bridge-Version", "1");
	httpRequest.timeoutSeconds = control.timeoutSeconds > 0 ? control.timeoutSeconds : 300;
	httpRequest.maxResponseBytes = 64U * 1024U * 1024U;
	httpRequest.shouldCancel = std::move(control.shouldCancel);
	const auto response = endpoint.perform(std::move(httpRequest));
	result.httpStatus = response.status;
	result.cancelled = response.cancelled;
	result.failure = response.cancelled ? RequestFailure::Cancelled : response.failure;
	if (response.cancelled) {
		result.error = response.error.empty() ? "request cancelled" : response.error;
		return result;
	}
	if (!response.started || result.failure != RequestFailure::None || response.status < 200 || response.status >= 300) {
		if (result.failure == RequestFailure::None) result.failure = response.status > 0
			? RequestFailure::Provider : RequestFailure::Transport;
		if (!response.body.empty() && response.body.size() <= 4096U) {
			result.error = response.body;
		} else {
			result.error = response.error.empty()
				? "SAM bridge returned HTTP " + std::to_string(response.status)
				: response.error;
		}
		return result;
	}
	if (response.body.size() < 3 || response.body[0] != 'P' ||
		(response.body[1] != '5' && response.body[1] != '2')) {
		result.failure = RequestFailure::InvalidResponse;
		result.error = "SAM bridge returned no PGM mask";
		return result;
	}
	result.maskBytes = response.body;
	result.success = true;
	return result;
}

} // namespace ofxIC
