#include "ofxICMediaClient.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <locale>
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

std::size_t matchingDelimiter(const std::string & json, std::size_t openPosition,
	char openDelimiter, char closeDelimiter) {
	int depth = 0;
	bool inString = false;
	bool escaped = false;
	for (std::size_t position = openPosition; position < json.size(); ++position) {
		const char character = json[position];
		if (inString) {
			if (escaped) escaped = false;
			else if (character == '\\') escaped = true;
			else if (character == '"') inString = false;
			continue;
		}
		if (character == '"') inString = true;
		else if (character == openDelimiter) ++depth;
		else if (character == closeDelimiter && --depth == 0) return position;
	}
	return std::string::npos;
}

std::string extractArrayField(const std::string & json, const std::string & key,
	std::size_t searchFrom = 0) {
	const std::string quoted = "\"" + key + "\"";
	const std::size_t keyPosition = json.find(quoted, searchFrom);
	if (keyPosition == std::string::npos) return {};
	std::size_t position = json.find(':', keyPosition + quoted.size());
	if (position == std::string::npos) return {};
	++position;
	while (position < json.size() &&
		std::isspace(static_cast<unsigned char>(json[position]))) ++position;
	if (position >= json.size() || json[position] != '[') return {};
	const std::size_t end = matchingDelimiter(json, position, '[', ']');
	return end == std::string::npos ? std::string{} :
		json.substr(position, end - position + 1U);
}

std::vector<std::string> extractStringArrayField(const std::string & json,
	const std::string & key, std::size_t searchFrom = 0) {
	const std::string array = extractArrayField(json, key, searchFrom);
	std::vector<std::string> result;
	std::size_t position = 1;
	while (position < array.size()) {
		while (position < array.size() && array[position] != '"') ++position;
		if (position >= array.size()) break;
		const std::string value = extractJsonStringAt(array, position);
		if (value.empty()) break;
		result.push_back(value);
		++position;
		bool escaped = false;
		for (; position < array.size(); ++position) {
			if (escaped) escaped = false;
			else if (array[position] == '\\') escaped = true;
			else if (array[position] == '"') { ++position; break; }
		}
	}
	return result;
}

int extractIntField(const std::string & json, const std::string & key,
	std::size_t searchFrom = 0) {
	const std::string quoted = "\"" + key + "\"";
	const std::size_t keyPosition = json.find(quoted, searchFrom);
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

void applyControl(HttpRequest & request, RequestControl control, int defaultTimeoutSeconds) {
	request.timeoutSeconds = control.timeoutSeconds > 0
		? control.timeoutSeconds : defaultTimeoutSeconds;
	request.shouldCancel = std::move(control.shouldCancel);
}

bool applyResponseFailure(const HttpResponse & response, bool & cancelled,
	RequestFailure & failure, std::string & error, const char * fallback) {
	cancelled = response.cancelled;
	failure = response.cancelled ? RequestFailure::Cancelled : response.failure;
	// HTTP headers may already say 200 when reading the body times out or is
	// cancelled. Never parse that partial body or overwrite the transport cause.
	if (response.started && response.status > 0 && failure == RequestFailure::None) return false;
	if (failure == RequestFailure::None) failure = RequestFailure::Transport;
	error = !response.error.empty() ? response.error :
		(failure == RequestFailure::Cancelled ? "request cancelled" :
			(failure == RequestFailure::Timeout ? "media request timed out" : fallback));
	return true;
}

MediaJob mediaFailure(MediaKind kind, std::string message,
	RequestFailure failure = RequestFailure::InvalidResponse, int httpStatus = 0,
	std::string rawResponse = {}) {
	MediaJob result;
	result.kind = kind;
	result.failure = failure;
	result.httpStatus = httpStatus;
	result.error = std::move(message);
	result.rawResponse = std::move(rawResponse);
	return result;
}

bool contains(const std::vector<std::string> & values, const std::string & value) {
	return std::find(values.begin(), values.end(), value) != values.end();
}

std::string lowerFilename(std::string value) {
	const std::size_t separator = value.find_last_of("/\\");
	if (separator != std::string::npos) value.erase(0, separator + 1U);
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

std::string preferredVideoFormat(const std::vector<std::string> & formats) {
	// MJPG AVI is decoded by the stock Windows Media Foundation player. WebM is
	// preferred only when AVI is unavailable because sd-server may be built
	// without its optional WebM encoder.
	for (const char * preferred : { "avi", "webm", "webp" })
		if (contains(formats, preferred)) return preferred;
	return {};
}

} // namespace

MediaClient::MediaClient(Endpoint & endpoint)
	: endpoint(endpoint) {
}

MediaCapabilities MediaClient::inspectCapabilities(RequestControl control) const {
	MediaCapabilities result;
	if (control.timeoutSeconds < 0) {
		result.failure = RequestFailure::InvalidResponse;
		result.error = "request timeout cannot be negative";
		return result;
	}
	HttpRequest request;
	request.method = HttpMethod::Get;
	request.url = "/sdcpp/v1/capabilities";
	request.accept = "application/json";
	request.useBearerToken = false;
	applyControl(request, std::move(control), 30);
	const HttpResponse response = endpoint.perform(std::move(request));
	result.httpStatus = response.status;
	result.rawResponse = response.body;
	if (applyResponseFailure(response, result.cancelled, result.failure, result.error,
		"could not inspect sd-server capabilities")) {
		return result;
	}
	if (response.status < 200 || response.status >= 300) {
		result.failure = RequestFailure::Provider;
		result.error = "sd-server capability endpoint returned HTTP " +
			std::to_string(response.status);
		const std::string detail = Endpoint::extractErrorText(response.body);
		if (!detail.empty()) result.error += ": " + detail;
		return result;
	}
	const std::size_t modelObject = response.body.find("\"model\"");
	result.model = modelObject == std::string::npos
		? std::string{} : extractStringField(response.body, "name", modelObject);
	result.currentMode = extractStringField(response.body, "current_mode");
	result.supportedModes = extractStringArrayField(response.body, "supported_modes");
	result.samplers = extractStringArrayField(response.body, "samplers");
	result.schedulers = extractStringArrayField(response.body, "schedulers");
	const std::size_t formatsObject = response.body.find("\"output_formats_by_mode\"");
	if (formatsObject != std::string::npos) {
		result.imageOutputFormats = extractStringArrayField(response.body, "img_gen", formatsObject);
		result.videoOutputFormats = extractStringArrayField(response.body, "vid_gen", formatsObject);
	}
	const std::size_t limitsObject = response.body.find("\"limits\"");
	if (limitsObject != std::string::npos) {
		result.minWidth = extractIntField(response.body, "min_width", limitsObject);
		result.maxWidth = extractIntField(response.body, "max_width", limitsObject);
		result.minHeight = extractIntField(response.body, "min_height", limitsObject);
		result.maxHeight = extractIntField(response.body, "max_height", limitsObject);
	}
	const std::size_t defaultsObject = response.body.find("\"defaults\"");
	if (defaultsObject != std::string::npos) {
		result.defaultWidth = extractIntField(response.body, "width", defaultsObject);
		result.defaultHeight = extractIntField(response.body, "height", defaultsObject);
		result.defaultVideoFrames = extractIntField(response.body, "video_frames", defaultsObject);
		result.defaultFps = extractIntField(response.body, "fps", defaultsObject);
		result.defaultOutputFormat = extractStringField(response.body, "output_format", defaultsObject);
	}
	result.success = true;
	return result;
}

ImageResult MediaClient::generateImage(const ImageRequest & request, RequestControl control) const {
	ImageResult result;
	if (control.timeoutSeconds < 0) {
		result.failure = RequestFailure::InvalidResponse;
		result.error = "request timeout cannot be negative";
		return result;
	}
	if (request.prompt.empty()) {
		result.failure = RequestFailure::InvalidResponse;
		result.error = "image prompt is empty";
		return result;
	}
	HttpRequest httpRequest;
	httpRequest.method = HttpMethod::Post;
	httpRequest.url = "/v1/images/generations";
	httpRequest.body = buildImageBody(request);
	applyControl(httpRequest, std::move(control), 180);
	const HttpResponse response = endpoint.perform(std::move(httpRequest));
	result.httpStatus = response.status;
	result.rawResponse = response.body;
	if (applyResponseFailure(response, result.cancelled, result.failure, result.error,
		"image request failed")) {
		return result;
	}
	if (response.status < 200 || response.status >= 300) {
		result.failure = RequestFailure::Provider;
		result.error = "image endpoint returned HTTP " + std::to_string(response.status);
		const std::string detail = Endpoint::extractErrorText(response.body);
		if (!detail.empty()) result.error += ": " + detail;
		else if (!response.error.empty()) result.error += ": " + response.error;
		return result;
	}
	result.outputFormat = extractStringField(response.body, "output_format");
	result.imagesBase64 = extractAllStringFields(response.body, "b64_json");
	result.urls = extractAllStringFields(response.body, "url");
	if (result.imagesBase64.empty() && result.urls.empty()) {
		result.failure = RequestFailure::InvalidResponse;
		result.error = "image endpoint returned no image";
		return result;
	}
	result.success = true;
	return result;
}

ImageResult MediaClient::downloadImage(const std::string & url, RequestControl control) const {
	ImageResult result;
	if (control.timeoutSeconds < 0 || (url.compare(0, 8, "https://") != 0 && url.compare(0, 7, "http://") != 0)) {
		result.failure = RequestFailure::InvalidResponse;
		result.error = "image download requires an HTTP(S) URL and non-negative timeout";
		return result;
	}
	HttpRequest request;
	request.method = HttpMethod::Get;
	request.url = url;
	request.accept = "image/png,image/jpeg,image/webp";
	request.useBearerToken = false;
	applyControl(request, std::move(control), 120);
	request.maxResponseBytes = 64U * 1024U * 1024U;
	const auto response = endpoint.perform(std::move(request));
	result.httpStatus = response.status;
	if (applyResponseFailure(response, result.cancelled, result.failure, result.error, "image download failed")) return result;
	if (response.status < 200 || response.status >= 300) {
		result.failure = RequestFailure::Provider;
		result.error = "image download returned HTTP " + std::to_string(response.status);
		return result;
	}
	const auto & bytes = response.body;
	if (bytes.size() >= 8 && bytes.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0) result.outputFormat = "png";
	else if (bytes.size() >= 3 && bytes.compare(0, 3, "\xff\xd8\xff", 3) == 0) result.outputFormat = "jpg";
	else if (bytes.size() >= 12 && bytes.compare(0, 4, "RIFF") == 0 && bytes.compare(8, 4, "WEBP") == 0) result.outputFormat = "webp";
	else {
		result.failure = RequestFailure::InvalidResponse;
		result.error = "image download did not return a PNG, JPEG or WebP image";
		return result;
	}
	result.imageBytes = bytes;
	result.success = true;
	return result;
}

MediaJob MediaClient::submit(const MediaJobRequest & request, RequestControl control) const {
	if (control.timeoutSeconds < 0) {
		MediaJob result;
		result.kind = request.kind;
		result.failure = RequestFailure::InvalidResponse;
		result.error = "request timeout cannot be negative";
		return result;
	}
	if (request.prompt.empty()) {
		MediaJob result;
		result.kind = request.kind;
		result.failure = RequestFailure::InvalidResponse;
		result.error = "media prompt is empty";
		return result;
	}
	MediaJobRequest effectiveRequest = request;
	if (!std::isfinite(request.guidance)) {
		return mediaFailure(request.kind, "media guidance must be a finite number",
			RequestFailure::Validation);
	}
	{
		const MediaCapabilities capabilities = inspectCapabilities(control);
		if (!capabilities) {
			MediaJob failed = mediaFailure(request.kind, capabilities.error,
				capabilities.failure, capabilities.httpStatus, capabilities.rawResponse);
			failed.cancelled = capabilities.cancelled;
			return failed;
		}
		const auto & modes = capabilities.supportedModes;
		const std::string & model = capabilities.model;
		if (!request.model.empty() && !model.empty() &&
			lowerFilename(request.model) != lowerFilename(model)) {
			return mediaFailure(request.kind,
				"sd-server loaded " + model + " but the selected context is " +
					lowerFilename(request.model) + "; restart sd-server to load the selected context");
		}
		const char * requiredMode = request.kind == MediaKind::Video ? "vid_gen" : "img_gen";
		if (!contains(modes, requiredMode)) {
			return mediaFailure(request.kind,
				"loaded sd-server model" + (model.empty() ? std::string{} : " " + model) +
				" does not support " +
				(request.kind == MediaKind::Video ? "video" : "image") +
				" generation; select a compatible model and restart sd-server");
		}
		if ((capabilities.minWidth > 0 && request.width < capabilities.minWidth) ||
			(capabilities.maxWidth > 0 && request.width > capabilities.maxWidth) ||
			(capabilities.minHeight > 0 && request.height < capabilities.minHeight) ||
			(capabilities.maxHeight > 0 && request.height > capabilities.maxHeight)) {
			return mediaFailure(request.kind,
				"requested media size " + std::to_string(request.width) + "x" +
				std::to_string(request.height) + " is outside the loaded context limits " +
				std::to_string(capabilities.minWidth) + "-" +
				std::to_string(capabilities.maxWidth) + " x " +
				std::to_string(capabilities.minHeight) + "-" +
				std::to_string(capabilities.maxHeight));
		}
		if (!request.sampleMethod.empty() && !capabilities.samplers.empty() &&
			!contains(capabilities.samplers, request.sampleMethod)) {
			return mediaFailure(request.kind, "loaded context does not support sampler " +
				request.sampleMethod);
		}
		if (!request.scheduler.empty() && !capabilities.schedulers.empty() &&
			!contains(capabilities.schedulers, request.scheduler)) {
			return mediaFailure(request.kind, "loaded context does not support scheduler " +
				request.scheduler);
		}
		const auto & modeFormats = request.kind == MediaKind::Video
			? capabilities.videoOutputFormats : capabilities.imageOutputFormats;
		if (!request.outputFormat.empty() && !modeFormats.empty() &&
			!contains(modeFormats, request.outputFormat)) {
			return mediaFailure(request.kind, "loaded context does not support " +
				request.outputFormat + " output for the selected mode");
		}
		if (request.kind != MediaKind::Video) {
			// A successful capability match is the external-process equivalent of
			// a loaded image context. Submission is safe only after this point.
		} else {
		const std::vector<std::string> & formats = capabilities.videoOutputFormats;
		if (effectiveRequest.outputFormat.empty()) {
			effectiveRequest.outputFormat = preferredVideoFormat(formats);
			if (effectiveRequest.outputFormat.empty()) {
				return mediaFailure(request.kind,
					"sd-server reports video support but no compatible AVI, WebM, or WebP output format");
			}
		} else if (!contains(formats, effectiveRequest.outputFormat)) {
			return mediaFailure(request.kind,
				"sd-server does not support requested video format " +
				effectiveRequest.outputFormat);
		}
		}
	}
	HttpRequest httpRequest;
	httpRequest.method = HttpMethod::Post;
	httpRequest.url = effectiveRequest.kind == MediaKind::Video
		? "/sdcpp/v1/vid_gen"
		: "/sdcpp/v1/img_gen";
	httpRequest.body = buildJobBody(effectiveRequest);
	httpRequest.useBearerToken = false;
	applyControl(httpRequest, std::move(control), 180);
	return parseJob(endpoint.perform(std::move(httpRequest)), effectiveRequest.kind);
}

MediaJob MediaClient::poll(const MediaJob & job, RequestControl control) const {
	if (job.protocol == MediaProtocol::HuggingFaceFal) {
		return pollHuggingFaceFal(job, std::move(control));
	}
	const std::string target = job.pollUrl.empty() ? job.id : job.pollUrl;
	MediaJob result = poll(target, std::move(control));
	if (result.kind == MediaKind::Image && job.kind == MediaKind::Video) {
		result.kind = MediaKind::Video;
	}
	return result;
}

MediaJob MediaClient::poll(const std::string & idOrPollUrl, RequestControl control) const {
	if (control.timeoutSeconds < 0) {
		MediaJob result;
		result.failure = RequestFailure::InvalidResponse;
		result.error = "request timeout cannot be negative";
		return result;
	}
	HttpRequest request;
	request.method = HttpMethod::Get;
	request.url = jobPath(idOrPollUrl);
	if (request.url.empty()) {
		MediaJob result;
		result.failure = RequestFailure::InvalidResponse;
		result.error = "media job id is empty";
		return result;
	}
	const std::string pollUrl = request.url;
	applyControl(request, std::move(control), 180);
	return parseJob(endpoint.perform(std::move(request)), MediaKind::Image, pollUrl);
}

MediaJob MediaClient::cancel(const MediaJob & job, RequestControl control) const {
	if (control.timeoutSeconds < 0) {
		MediaJob result = job;
		result.success = false;
		result.failure = RequestFailure::InvalidResponse;
		result.error = "request timeout cannot be negative";
		return result;
	}
	if (job.protocol == MediaProtocol::HuggingFaceFal) {
		MediaJob result = job;
		result.success = false;
		result.failure = RequestFailure::InvalidResponse;
		result.error = "Hugging Face / fal-ai cancellation is not supported yet";
		return result;
	}
	HttpRequest request;
	request.method = HttpMethod::Post;
	request.url = jobPath(job.pollUrl.empty() ? job.id : job.pollUrl);
	if (request.url.empty()) {
		MediaJob result;
		result.kind = job.kind;
		result.failure = RequestFailure::InvalidResponse;
		result.error = "media job id is empty";
		return result;
	}
	request.url += "/cancel";
	request.body = "{}";
	applyControl(request, std::move(control), 180);
	MediaJob result = parseJob(endpoint.perform(std::move(request)), job.kind, job.pollUrl);
	if (result.httpStatus >= 200 && result.httpStatus < 300 &&
		result.state == MediaJobState::Cancelled) {
		result.success = true;
		result.cancelled = false;
		result.failure = RequestFailure::None;
		result.error.clear();
	}
	return result;
}

std::string MediaClient::buildImageBody(const ImageRequest & request) {
	std::ostringstream body;
	body.imbue(std::locale::classic());
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
	body.imbue(std::locale::classic());
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
		 << ",\"guidance\":{\"txt_cfg\":" << std::max(0.0f, request.guidance) << "}";
	if (!request.sampleMethod.empty())
		body << ",\"sample_method\":\"" << escapeJson(request.sampleMethod) << "\"";
	if (!request.scheduler.empty())
		body << ",\"scheduler\":\"" << escapeJson(request.scheduler) << "\"";
	body << "}"
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
	if (applyResponseFailure(response, result.cancelled, result.failure, result.error,
		"media job request failed")) {
		return result;
	}
	if (response.status < 200 || response.status >= 300) {
		result.failure = RequestFailure::Provider;
		result.error = "media endpoint returned HTTP " + std::to_string(response.status);
		std::string detail = extractStringField(response.body, "message");
		if (detail.empty()) detail = extractStringField(response.body, "error");
		if (detail.empty()) detail = response.error;
		if (!detail.empty()) result.error += ": " + detail;
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
		result.failure = RequestFailure::Provider;
		result.error = extractStringField(response.body, "message");
		if (result.error.empty()) result.error = "media job did not complete";
		return result;
	}
	if (result.state == MediaJobState::Completed && result.payloadsBase64.empty()) {
		result.failure = RequestFailure::InvalidResponse;
		result.error = "completed media job returned no payload";
		return result;
	}
	if (result.id.empty() && result.state == MediaJobState::Unknown) {
		result.failure = RequestFailure::InvalidResponse;
		result.error = "media endpoint returned no job";
		return result;
	}
	result.success = true;
	return result;
}

} // namespace ofxIC
