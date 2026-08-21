#include "ofxICMediaClient.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace ofxIC {
namespace {

std::string escapeJson(const std::string & value) {
	std::ostringstream escaped;
	for (const unsigned char character : value) {
		switch (character) {
		case '"': escaped << "\\\""; break;
		case '\\': escaped << "\\\\"; break;
		case '\b': escaped << "\\b"; break;
		case '\f': escaped << "\\f"; break;
		case '\n': escaped << "\\n"; break;
		case '\r': escaped << "\\r"; break;
		case '\t': escaped << "\\t"; break;
		default:
			if (character < 0x20U) {
				const char hex[] = "0123456789abcdef";
				escaped << "\\u00" << hex[character >> 4U] << hex[character & 0x0fU];
			} else {
				escaped << static_cast<char>(character);
			}
		}
	}
	return escaped.str();
}

std::string extractJsonStringAt(const std::string & json, std::size_t position) {
	while (position < json.size() &&
		std::isspace(static_cast<unsigned char>(json[position]))) ++position;
	if (position >= json.size() || json[position] != '"') return {};
	++position;
	std::string value;
	bool escaped = false;
	for (; position < json.size(); ++position) {
		const char character = json[position];
		if (escaped) {
			switch (character) {
			case 'n': value.push_back('\n'); break;
			case 'r': value.push_back('\r'); break;
			case 't': value.push_back('\t'); break;
			default: value.push_back(character); break;
			}
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

std::string extractStringField(
	const std::string & json,
	const std::string & key,
	std::size_t searchFrom = 0) {
	const std::string quoted = "\"" + key + "\"";
	const std::size_t keyPosition = json.find(quoted, searchFrom);
	if (keyPosition == std::string::npos) return {};
	const std::size_t colon = json.find(':', keyPosition + quoted.size());
	return colon == std::string::npos ? std::string() : extractJsonStringAt(json, colon + 1);
}

std::vector<std::string> extractAllStringFields(
	const std::string & json,
	const std::string & key) {
	std::vector<std::string> values;
	const std::string quoted = "\"" + key + "\"";
	std::size_t searchFrom = 0;
	while (true) {
		const std::size_t keyPosition = json.find(quoted, searchFrom);
		if (keyPosition == std::string::npos) break;
		const std::size_t colon = json.find(':', keyPosition + quoted.size());
		if (colon == std::string::npos) break;
		const std::string value = extractJsonStringAt(json, colon + 1);
		if (!value.empty()) values.push_back(value);
		searchFrom = colon + 1;
	}
	return values;
}

int extractIntField(const std::string & json, const std::string & key) {
	const std::string quoted = "\"" + key + "\"";
	const std::size_t keyPosition = json.find(quoted);
	if (keyPosition == std::string::npos) return 0;
	const std::size_t colon = json.find(':', keyPosition + quoted.size());
	if (colon == std::string::npos) return 0;
	return std::atoi(json.c_str() + colon + 1);
}

MediaJobState parseState(const std::string & value) {
	if (value == "queued") return MediaJobState::Queued;
	if (value == "generating" || value == "in_progress") return MediaJobState::Generating;
	if (value == "completed") return MediaJobState::Completed;
	if (value == "failed") return MediaJobState::Failed;
	if (value == "cancelled" || value == "canceled") return MediaJobState::Cancelled;
	return MediaJobState::Unknown;
}

MediaKind parseKind(const std::string & value, MediaKind fallback) {
	if (value == "img_gen" || value == "image") return MediaKind::Image;
	if (value == "vid_gen" || value == "video") return MediaKind::Video;
	return fallback;
}

std::string jobPath(const std::string & idOrPollUrl) {
	if (idOrPollUrl.empty()) return {};
	if (idOrPollUrl.front() == '/' ||
		idOrPollUrl.compare(0, 7, "http://") == 0 ||
		idOrPollUrl.compare(0, 8, "https://") == 0) return idOrPollUrl;
	return "/sdcpp/v1/jobs/" + idOrPollUrl;
}

} // namespace

MediaClient::MediaClient(Endpoint & endpoint)
	: endpoint(endpoint) {
}

ImageResult MediaClient::generateImage(const ImageRequest & request) const {
	ImageResult result;
	if (request.prompt.empty()) {
		result.error = "image prompt is empty";
		return result;
	}
	HttpRequest httpRequest;
	httpRequest.method = HttpMethod::Post;
	httpRequest.url = "/v1/images/generations";
	httpRequest.body = buildImageBody(request);
	const HttpResponse response = endpoint.perform(std::move(httpRequest));
	result.httpStatus = response.status;
	result.rawResponse = response.body;
	if (!response.started) {
		result.error = response.error.empty() ? "image request did not start" : response.error;
		return result;
	}
	if (response.status < 200 || response.status >= 300) {
		result.error = "image endpoint returned HTTP " + std::to_string(response.status);
		if (!response.error.empty()) result.error += ": " + response.error;
		return result;
	}
	result.outputFormat = extractStringField(response.body, "output_format");
	result.imagesBase64 = extractAllStringFields(response.body, "b64_json");
	result.urls = extractAllStringFields(response.body, "url");
	if (result.imagesBase64.empty() && result.urls.empty()) {
		result.error = "image endpoint returned no image";
		return result;
	}
	result.success = true;
	return result;
}

MediaJob MediaClient::submit(const MediaJobRequest & request) const {
	if (request.prompt.empty()) {
		MediaJob result;
		result.kind = request.kind;
		result.error = "media prompt is empty";
		return result;
	}
	HttpRequest httpRequest;
	httpRequest.method = HttpMethod::Post;
	httpRequest.url = request.kind == MediaKind::Video
		? "/sdcpp/v1/vid_gen"
		: "/sdcpp/v1/img_gen";
	httpRequest.body = buildJobBody(request);
	return parseJob(endpoint.perform(std::move(httpRequest)), request.kind);
}

MediaJob MediaClient::poll(const MediaJob & job) const {
	const std::string target = job.pollUrl.empty() ? job.id : job.pollUrl;
	MediaJob result = poll(target);
	if (result.kind == MediaKind::Image && job.kind == MediaKind::Video) {
		result.kind = MediaKind::Video;
	}
	return result;
}

MediaJob MediaClient::poll(const std::string & idOrPollUrl) const {
	HttpRequest request;
	request.method = HttpMethod::Get;
	request.url = jobPath(idOrPollUrl);
	if (request.url.empty()) {
		MediaJob result;
		result.error = "media job id is empty";
		return result;
	}
	const std::string pollUrl = request.url;
	return parseJob(endpoint.perform(std::move(request)), MediaKind::Image, pollUrl);
}

MediaJob MediaClient::cancel(const MediaJob & job) const {
	HttpRequest request;
	request.method = HttpMethod::Post;
	request.url = jobPath(job.pollUrl.empty() ? job.id : job.pollUrl);
	if (request.url.empty()) {
		MediaJob result;
		result.kind = job.kind;
		result.error = "media job id is empty";
		return result;
	}
	request.url += "/cancel";
	request.body = "{}";
	MediaJob result = parseJob(endpoint.perform(std::move(request)), job.kind, job.pollUrl);
	if (result.httpStatus >= 200 && result.httpStatus < 300 &&
		result.state == MediaJobState::Cancelled) {
		result.success = true;
		result.error.clear();
	}
	return result;
}

std::string MediaClient::buildImageBody(const ImageRequest & request) {
	std::ostringstream body;
	body << "{\"prompt\":\"" << escapeJson(request.prompt) << "\"";
	if (!request.model.empty()) body << ",\"model\":\"" << escapeJson(request.model) << "\"";
	body << ",\"n\":" << std::max(1, request.count)
		 << ",\"size\":\"" << std::max(1, request.width) << "x" << std::max(1, request.height) << "\""
		 << ",\"output_format\":\"" << escapeJson(request.outputFormat) << "\"}";
	return body.str();
}

std::string MediaClient::buildJobBody(const MediaJobRequest & request) {
	const bool video = request.kind == MediaKind::Video;
	const std::string outputFormat = request.outputFormat.empty()
		? (video ? "webm" : "png")
		: request.outputFormat;
	std::ostringstream body;
	body << "{\"prompt\":\"" << escapeJson(request.prompt) << "\""
		 << ",\"negative_prompt\":\"" << escapeJson(request.negativePrompt) << "\""
		 << ",\"width\":" << std::max(1, request.width)
		 << ",\"height\":" << std::max(1, request.height)
		 << ",\"seed\":" << request.seed;
	if (video) {
		body << ",\"video_frames\":" << std::max(1, request.videoFrames)
			 << ",\"fps\":" << std::max(1, request.fps);
	} else {
		body << ",\"batch_count\":" << std::max(1, request.imageCount);
	}
	body << ",\"sample_params\":{\"sample_steps\":" << std::max(1, request.steps)
		 << ",\"guidance\":{\"txt_cfg\":" << std::max(0.0f, request.guidance) << "}}"
		 << ",\"output_format\":\"" << escapeJson(outputFormat) << "\"}";
	return body.str();
}

MediaJob MediaClient::parseJob(
	const HttpResponse & response,
	MediaKind fallbackKind,
	std::string fallbackPollUrl) {
	MediaJob result;
	result.kind = fallbackKind;
	result.httpStatus = response.status;
	result.rawResponse = response.body;
	result.pollUrl = std::move(fallbackPollUrl);
	if (!response.started) {
		result.error = response.error.empty() ? "media job request did not start" : response.error;
		return result;
	}
	if (response.status < 200 || response.status >= 300) {
		result.error = "media endpoint returned HTTP " + std::to_string(response.status);
		if (!response.error.empty()) result.error += ": " + response.error;
		return result;
	}
	result.id = extractStringField(response.body, "id");
	result.kind = parseKind(extractStringField(response.body, "kind"), fallbackKind);
	result.state = parseState(extractStringField(response.body, "status"));
	const std::string returnedPollUrl = extractStringField(response.body, "poll_url");
	if (!returnedPollUrl.empty()) result.pollUrl = returnedPollUrl;
	result.outputFormat = extractStringField(response.body, "output_format");
	result.mimeType = extractStringField(response.body, "mime_type");
	result.fps = extractIntField(response.body, "fps");
	result.frameCount = extractIntField(response.body, "frame_count");
	result.payloadsBase64 = extractAllStringFields(response.body, "b64_json");
	if (result.state == MediaJobState::Failed || result.state == MediaJobState::Cancelled) {
		result.error = extractStringField(response.body, "message");
		if (result.error.empty()) result.error = "media job did not complete";
		return result;
	}
	if (result.state == MediaJobState::Completed && result.payloadsBase64.empty()) {
		result.error = "completed media job returned no payload";
		return result;
	}
	if (result.id.empty() && result.state == MediaJobState::Unknown) {
		result.error = "media endpoint returned no job";
		return result;
	}
	result.success = true;
	return result;
}

} // namespace ofxIC
