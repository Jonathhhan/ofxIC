#include "ofxICMediaClient.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
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

std::size_t findMatchingDelimiter(
	const std::string & json,
	std::size_t openPosition,
	char openDelimiter,
	char closeDelimiter) {
	if (openPosition >= json.size() || json[openPosition] != openDelimiter) {
		return std::string::npos;
	}
	int depth = 0;
	bool inString = false;
	bool escaped = false;
	for (std::size_t index = openPosition; index < json.size(); ++index) {
		const char character = json[index];
		if (inString) {
			if (escaped) escaped = false;
			else if (character == '\\') escaped = true;
			else if (character == '"') inString = false;
			continue;
		}
		if (character == '"') inString = true;
		else if (character == openDelimiter) ++depth;
		else if (character == closeDelimiter && --depth == 0) return index;
	}
	return std::string::npos;
}

std::string extractObjectField(const std::string & json, const std::string & key) {
	const std::string quoted = "\"" + key + "\"";
	const std::size_t keyPosition = json.find(quoted);
	if (keyPosition == std::string::npos) return {};
	const std::size_t colon = json.find(':', keyPosition + quoted.size());
	if (colon == std::string::npos) return {};
	const std::size_t objectStart = json.find('{', colon + 1);
	if (objectStart == std::string::npos) return {};
	const std::size_t objectEnd = findMatchingDelimiter(json, objectStart, '{', '}');
	return objectEnd == std::string::npos
		? std::string()
		: json.substr(objectStart, objectEnd - objectStart + 1);
}

std::string encodeModelPath(const std::string & value) {
	std::ostringstream encoded;
	encoded << std::uppercase << std::hex;
	for (const unsigned char character : value) {
		if (std::isalnum(character) || character == '-' || character == '_' ||
			character == '.' || character == '~' || character == '/') {
			encoded << static_cast<char>(character);
		} else {
			encoded << '%' << std::setw(2) << std::setfill('0')
				<< static_cast<int>(character);
		}
	}
	return encoded.str();
}

std::string urlPath(const std::string & url) {
	const std::size_t scheme = url.find("://");
	const std::size_t pathStart = scheme == std::string::npos
		? (url.empty() || url.front() != '/' ? std::string::npos : 0)
		: url.find('/', scheme + 3);
	if (pathStart == std::string::npos) return {};
	const std::size_t end = url.find_first_of("?#", pathStart);
	return url.substr(pathStart, end == std::string::npos ? end : end - pathStart);
}

std::string withQueueRoute(std::string url) {
	url += url.find('?') == std::string::npos ? "?_subdomain=queue" : "&_subdomain=queue";
	return url;
}

std::string formatFromUrl(const std::string & url, MediaKind kind) {
	const std::size_t end = url.find_first_of("?#");
	const std::string path = url.substr(0, end);
	const std::size_t dot = path.find_last_of('.');
	std::string extension = dot == std::string::npos ? std::string() : path.substr(dot + 1);
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	if (extension == "jpeg") extension = "jpg";
	for (const char * supported : { "png", "jpg", "webp", "mp4", "webm", "avi" }) {
		if (extension == supported) return extension;
	}
	return kind == MediaKind::Video ? "mp4" : "png";
}

std::string mimeForFormat(const std::string & format, MediaKind kind) {
	if (format == "jpg") return "image/jpeg";
	if (format == "png" || format == "webp") return "image/" + format;
	if (format == "avi") return "video/x-msvideo";
	if (format == "mp4" || format == "webm") return "video/" + format;
	return kind == MediaKind::Video ? "video/mp4" : "image/png";
}

MediaJobState parseHuggingFaceState(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::toupper(c));
	});
	if (value == "IN_QUEUE" || value == "QUEUED") return MediaJobState::Queued;
	if (value == "IN_PROGRESS" || value == "RUNNING") return MediaJobState::Generating;
	if (value == "COMPLETED" || value == "SUCCEEDED") return MediaJobState::Completed;
	if (value == "FAILED" || value == "ERROR") return MediaJobState::Failed;
	if (value == "CANCELLED" || value == "CANCELED") return MediaJobState::Cancelled;
	return MediaJobState::Unknown;
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

MediaJob MediaClient::submitHuggingFace(const MediaJobRequest & request) const {
	MediaJob job;
	job.kind = request.kind;
	job.protocol = MediaProtocol::HuggingFace;
	job.provider = request.provider;
	job.outputFormat = request.outputFormat.empty()
		? (request.kind == MediaKind::Video ? "mp4" : "png")
		: request.outputFormat;
	job.mimeType = mimeForFormat(job.outputFormat, request.kind);
	job.fps = request.kind == MediaKind::Video ? std::max(1, request.fps) : 0;
	job.frameCount = request.kind == MediaKind::Video ? std::max(1, request.videoFrames) : 0;
	if (request.prompt.empty()) {
		job.error = "media prompt is empty";
		return job;
	}
	if (request.model.empty()) {
		job.error = "Hugging Face media model is empty";
		return job;
	}
	if (request.provider != "fal-ai") {
		job.error = "Hugging Face media provider is not supported: " + request.provider;
		return job;
	}

	HttpRequest mappingRequest;
	mappingRequest.method = HttpMethod::Get;
	mappingRequest.url = "https://huggingface.co/api/models/" +
		encodeModelPath(request.model) + "?expand=inferenceProviderMapping";
	mappingRequest.useBearerToken = false;
	mappingRequest.timeoutSeconds = 30;
	const HttpResponse mappingResponse = endpoint.perform(std::move(mappingRequest));
	job.httpStatus = mappingResponse.status;
	if (!mappingResponse.started || mappingResponse.status < 200 || mappingResponse.status >= 300) {
		job.error = mappingResponse.error.empty()
			? "could not resolve Hugging Face media model"
			: mappingResponse.error;
		return job;
	}
	const std::string mapping = extractObjectField(mappingResponse.body, request.provider);
	const std::string providerModel = extractStringField(mapping, "providerId");
	const std::string expectedTask = request.kind == MediaKind::Video
		? "text-to-video"
		: "text-to-image";
	const std::string mappedTask = extractStringField(mapping, "task");
	if (providerModel.empty()) {
		job.error = request.model + " is not available from " + request.provider;
		return job;
	}
	if (mappedTask != expectedTask) {
		job.error = request.model + " does not provide " + expectedTask +
			" through " + request.provider;
		return job;
	}

	HttpRequest submitRequest;
	submitRequest.method = HttpMethod::Post;
	submitRequest.url = "https://router.huggingface.co/fal-ai/" + providerModel;
	if (request.kind == MediaKind::Video) {
		submitRequest.url = withQueueRoute(std::move(submitRequest.url));
	}
	submitRequest.body = buildHuggingFaceBody(request);
	submitRequest.timeoutSeconds = request.kind == MediaKind::Video ? 60 : 300;
	const HttpResponse response = endpoint.perform(std::move(submitRequest));
	job.httpStatus = response.status;
	job.rawResponse = response.body;
	if (!response.started) {
		job.error = response.error.empty() ? "Hugging Face media request did not start" : response.error;
		return job;
	}
	if (response.status < 200 || response.status >= 300) {
		job.error = "Hugging Face media endpoint returned HTTP " + std::to_string(response.status);
		const std::string message = extractStringField(response.body, "message");
		if (!message.empty()) job.error += ": " + message;
		else if (!response.error.empty()) job.error += ": " + response.error;
		return job;
	}

	if (request.kind == MediaKind::Image) {
		const std::string outputUrl = extractStringField(response.body, "url");
		if (outputUrl.empty()) {
			job.error = "Hugging Face image response returned no output URL";
			return job;
		}
		job.id = "hf-image";
		job.state = MediaJobState::Completed;
		job.resultUrl = outputUrl;
		downloadHuggingFaceOutput(outputUrl, job);
		return job;
	}

	job.id = extractStringField(response.body, "request_id");
	const std::string responseUrl = extractStringField(response.body, "response_url");
	const std::string responsePath = urlPath(responseUrl);
	if (job.id.empty() || responsePath.empty()) {
		job.error = "Hugging Face video response returned no queue job";
		return job;
	}
	const std::string routerResult = "https://router.huggingface.co/fal-ai" + responsePath;
	job.pollUrl = withQueueRoute(routerResult + "/status");
	job.resultUrl = withQueueRoute(routerResult);
	job.state = parseHuggingFaceState(extractStringField(response.body, "status"));
	if (job.state == MediaJobState::Unknown) job.state = MediaJobState::Queued;
	job.success = true;
	return job.state == MediaJobState::Completed ? pollHuggingFace(job) : job;
}

MediaJob MediaClient::poll(const MediaJob & job) const {
	if (job.protocol == MediaProtocol::HuggingFace) {
		return pollHuggingFace(job);
	}
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
	if (job.protocol == MediaProtocol::HuggingFace) {
		MediaJob result = job;
		result.success = false;
		result.error = "Hugging Face media cancellation is not supported yet";
		return result;
	}
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

std::string MediaClient::buildHuggingFaceBody(const MediaJobRequest & request) {
	const bool video = request.kind == MediaKind::Video;
	std::ostringstream body;
	body << "{\"prompt\":\"" << escapeJson(request.prompt) << "\"";
	if (!request.negativePrompt.empty()) {
		if (video) {
			body << ",\"negative_prompt\":[\"" << escapeJson(request.negativePrompt) << "\"]";
		} else {
			body << ",\"negative_prompt\":\"" << escapeJson(request.negativePrompt) << "\"";
		}
	}
	if (video) {
		body << ",\"num_frames\":" << std::max(1, request.videoFrames);
	} else {
		body << ",\"image_size\":{\"width\":" << std::max(1, request.width)
			 << ",\"height\":" << std::max(1, request.height) << "}";
	}
	body << ",\"num_inference_steps\":" << std::max(1, request.steps)
		 << ",\"guidance_scale\":" << std::max(0.0f, request.guidance);
	if (request.seed >= 0) body << ",\"seed\":" << request.seed;
	body << "}";
	return body.str();
}

MediaJob MediaClient::pollHuggingFace(const MediaJob & job) const {
	MediaJob result = job;
	result.success = false;
	result.error.clear();
	if (job.pollUrl.empty() || job.resultUrl.empty()) {
		result.error = "Hugging Face media job has no polling URL";
		return result;
	}

	HttpRequest statusRequest;
	statusRequest.method = HttpMethod::Get;
	statusRequest.url = job.pollUrl;
	statusRequest.timeoutSeconds = 30;
	const HttpResponse statusResponse = endpoint.perform(std::move(statusRequest));
	result.httpStatus = statusResponse.status;
	result.rawResponse = statusResponse.body;
	if (!statusResponse.started) {
		result.error = statusResponse.error.empty()
			? "Hugging Face media poll did not start"
			: statusResponse.error;
		return result;
	}
	if (statusResponse.status < 200 || statusResponse.status >= 300) {
		result.error = "Hugging Face media poll returned HTTP " +
			std::to_string(statusResponse.status);
		return result;
	}

	result.state = parseHuggingFaceState(extractStringField(statusResponse.body, "status"));
	if (result.state == MediaJobState::Queued || result.state == MediaJobState::Generating) {
		result.success = true;
		return result;
	}
	if (result.state == MediaJobState::Failed || result.state == MediaJobState::Cancelled) {
		result.error = extractStringField(statusResponse.body, "error");
		if (result.error.empty()) result.error = extractStringField(statusResponse.body, "message");
		if (result.error.empty()) result.error = "Hugging Face media job did not complete";
		return result;
	}
	if (result.state != MediaJobState::Completed) {
		result.error = "Hugging Face media job returned an unknown state";
		return result;
	}

	HttpRequest outputRequest;
	outputRequest.method = HttpMethod::Get;
	outputRequest.url = job.resultUrl;
	outputRequest.timeoutSeconds = 60;
	const HttpResponse outputResponse = endpoint.perform(std::move(outputRequest));
	result.httpStatus = outputResponse.status;
	result.rawResponse = outputResponse.body;
	if (!outputResponse.started || outputResponse.status < 200 || outputResponse.status >= 300) {
		result.error = outputResponse.error.empty()
			? "Hugging Face media result is not available"
			: outputResponse.error;
		return result;
	}
	const std::string outputUrl = extractStringField(outputResponse.body, "url");
	if (outputUrl.empty()) {
		result.error = "Hugging Face media result returned no output URL";
		return result;
	}
	result.resultUrl = outputUrl;
	downloadHuggingFaceOutput(outputUrl, result);
	return result;
}

bool MediaClient::downloadHuggingFaceOutput(const std::string & url, MediaJob & job) const {
	HttpRequest downloadRequest;
	downloadRequest.method = HttpMethod::Get;
	downloadRequest.url = url;
	downloadRequest.accept = "*/*";
	downloadRequest.useBearerToken = false;
	downloadRequest.timeoutSeconds = 300;
	const HttpResponse response = endpoint.perform(std::move(downloadRequest));
	job.httpStatus = response.status;
	if (!response.started) {
		job.success = false;
		job.error = response.error.empty() ? "media download did not start" : response.error;
		return false;
	}
	if (response.status < 200 || response.status >= 300) {
		job.success = false;
		job.error = "media download returned HTTP " + std::to_string(response.status);
		return false;
	}
	if (response.body.empty()) {
		job.success = false;
		job.error = "media download returned no bytes";
		return false;
	}
	job.outputFormat = formatFromUrl(url, job.kind);
	job.mimeType = mimeForFormat(job.outputFormat, job.kind);
	job.payloadBytes = { response.body };
	job.state = MediaJobState::Completed;
	job.success = true;
	job.error.clear();
	return true;
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
