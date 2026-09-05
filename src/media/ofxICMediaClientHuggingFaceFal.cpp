#include "ofxICMediaClient.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>

namespace ofxIC {
namespace {

constexpr const char * providerName = "fal-ai";
constexpr const char * routerBaseUrl = "https://router.huggingface.co/fal-ai";

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

std::string extractStringField(const std::string & json, const std::string & key) {
	const std::string quoted = "\"" + key + "\"";
	const std::size_t keyPosition = json.find(quoted);
	if (keyPosition == std::string::npos) return {};
	const std::size_t colon = json.find(':', keyPosition + quoted.size());
	return colon == std::string::npos ? std::string() : extractJsonStringAt(json, colon + 1);
}

std::size_t findMatchingObjectEnd(const std::string & json, std::size_t openPosition) {
	if (openPosition >= json.size() || json[openPosition] != '{') return std::string::npos;
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
		else if (character == '{') ++depth;
		else if (character == '}' && --depth == 0) return index;
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
	const std::size_t objectEnd = findMatchingObjectEnd(json, objectStart);
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

MediaJobState parseQueueState(std::string value) {
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

void applyControl(HttpRequest & request, const RequestControl & control,
	int defaultTimeoutSeconds) {
	request.timeoutSeconds = control.timeoutSeconds > 0
		? control.timeoutSeconds : defaultTimeoutSeconds;
	request.shouldCancel = control.shouldCancel;
}

bool applyResponseFailure(const HttpResponse & response, MediaJob & job) {
	job.cancelled = response.cancelled;
	job.failure = response.cancelled ? RequestFailure::Cancelled : response.failure;
	job.httpStatus = response.status;
	if (response.started && response.status > 0 && job.failure == RequestFailure::None) return false;
	if (job.failure == RequestFailure::None) job.failure = RequestFailure::Transport;
	job.success = false;
	job.error = !response.error.empty() ? response.error :
		(job.failure == RequestFailure::Cancelled ? "request cancelled" :
			(job.failure == RequestFailure::Timeout ? "media request timed out" : "media transport failed"));
	return true;
}

} // namespace

MediaJob MediaClient::submitHuggingFaceFal(
	const MediaJobRequest & request, RequestControl control) const {
	MediaJob job;
	job.kind = request.kind;
	job.protocol = MediaProtocol::HuggingFaceFal;
	job.outputFormat = request.outputFormat.empty()
		? (request.kind == MediaKind::Video ? "mp4" : "png")
		: request.outputFormat;
	job.mimeType = mimeForFormat(job.outputFormat, request.kind);
	job.fps = request.kind == MediaKind::Video ? std::max(1, request.fps) : 0;
	job.frameCount = request.kind == MediaKind::Video ? std::max(1, request.videoFrames) : 0;
	if (control.timeoutSeconds < 0) {
		job.failure = RequestFailure::Validation;
		job.error = "request timeout cannot be negative";
		return job;
	}
	if (request.prompt.empty()) {
		job.failure = RequestFailure::Validation;
		job.error = "media prompt is empty";
		return job;
	}
	if (request.model.empty()) {
		job.failure = RequestFailure::Validation;
		job.error = "Hugging Face / fal-ai media model is empty";
		return job;
	}
	if (!std::isfinite(request.guidance)) {
		job.failure = RequestFailure::Validation;
		job.error = "media guidance must be a finite number";
		return job;
	}

	HttpRequest mappingRequest;
	mappingRequest.method = HttpMethod::Get;
	mappingRequest.url = "https://huggingface.co/api/models/" +
		encodeModelPath(request.model) + "?expand=inferenceProviderMapping";
	mappingRequest.useBearerToken = false;
	applyControl(mappingRequest, control, 30);
	const HttpResponse mappingResponse = endpoint.perform(std::move(mappingRequest));
	if (applyResponseFailure(mappingResponse, job)) return job;
	if (mappingResponse.status < 200 || mappingResponse.status >= 300) {
		if (job.failure == RequestFailure::None) job.failure = mappingResponse.status > 0
			? RequestFailure::Provider : RequestFailure::Transport;
		job.error = mappingResponse.error.empty()
			? "could not resolve Hugging Face media model"
			: mappingResponse.error;
		return job;
	}
	const std::string mapping = extractObjectField(mappingResponse.body, providerName);
	const std::string providerModel = extractStringField(mapping, "providerId");
	const std::string expectedTask = request.kind == MediaKind::Video
		? "text-to-video"
		: "text-to-image";
	const std::string mappedTask = extractStringField(mapping, "task");
	if (providerModel.empty()) {
		job.failure = RequestFailure::InvalidResponse;
		job.error = request.model + " is not available from fal-ai";
		return job;
	}
	if (mappedTask != expectedTask) {
		job.failure = RequestFailure::InvalidResponse;
		job.error = request.model + " does not provide " + expectedTask + " through fal-ai";
		return job;
	}

	HttpRequest submitRequest;
	submitRequest.method = HttpMethod::Post;
	submitRequest.url = std::string(routerBaseUrl) + "/" + providerModel;
	if (request.kind == MediaKind::Video) {
		submitRequest.url = withQueueRoute(std::move(submitRequest.url));
	}
	submitRequest.body = buildHuggingFaceFalBody(request);
	applyControl(submitRequest, control, request.kind == MediaKind::Video ? 60 : 300);
	const HttpResponse response = endpoint.perform(std::move(submitRequest));
	job.rawResponse = response.body;
	if (applyResponseFailure(response, job)) return job;
	if (response.status < 200 || response.status >= 300) {
		job.failure = RequestFailure::Provider;
		job.error = "Hugging Face / fal-ai returned HTTP " + std::to_string(response.status);
		if (response.status == 402) {
			job.error += ": inference credits or pay-as-you-go billing are required";
		}
		const std::string message = extractStringField(response.body, "message");
		if (!message.empty()) job.error += ": " + message;
		else if (!response.error.empty()) job.error += ": " + response.error;
		return job;
	}

	if (request.kind == MediaKind::Image) {
		const std::string outputUrl = extractStringField(response.body, "url");
		if (outputUrl.empty()) {
			job.failure = RequestFailure::InvalidResponse;
			job.error = "Hugging Face / fal-ai image response returned no output URL";
			return job;
		}
		job.id = "hf-fal-image";
		job.state = MediaJobState::Completed;
		job.resultUrl = outputUrl;
		downloadHuggingFaceFalOutput(outputUrl, job, control);
		return job;
	}

	job.id = extractStringField(response.body, "request_id");
	const std::string responseUrl = extractStringField(response.body, "response_url");
	const std::string responsePath = urlPath(responseUrl);
	if (job.id.empty() || responsePath.empty()) {
		job.failure = RequestFailure::InvalidResponse;
		job.error = "Hugging Face / fal-ai video response returned no queue job";
		return job;
	}
	const std::string routerResult = std::string(routerBaseUrl) + responsePath;
	job.pollUrl = withQueueRoute(routerResult + "/status");
	job.resultUrl = withQueueRoute(routerResult);
	job.state = parseQueueState(extractStringField(response.body, "status"));
	if (job.state == MediaJobState::Unknown) job.state = MediaJobState::Queued;
	job.success = true;
	return job.state == MediaJobState::Completed ? pollHuggingFaceFal(job, control) : job;
}

std::string MediaClient::buildHuggingFaceFalBody(const MediaJobRequest & request) {
	const bool video = request.kind == MediaKind::Video;
	std::ostringstream body;
	body.imbue(std::locale::classic());
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

MediaJob MediaClient::pollHuggingFaceFal(
	const MediaJob & job, RequestControl control) const {
	MediaJob result = job;
	result.success = false;
	result.error.clear();
	result.cancelled = false;
	result.failure = RequestFailure::None;
	if (control.timeoutSeconds < 0) {
		result.failure = RequestFailure::Validation;
		result.error = "request timeout cannot be negative";
		return result;
	}
	if (job.pollUrl.empty() || job.resultUrl.empty()) {
		result.failure = RequestFailure::Validation;
		result.error = "Hugging Face / fal-ai job has no polling URL";
		return result;
	}

	HttpRequest statusRequest;
	statusRequest.method = HttpMethod::Get;
	statusRequest.url = job.pollUrl;
	applyControl(statusRequest, control, 30);
	const HttpResponse statusResponse = endpoint.perform(std::move(statusRequest));
	result.rawResponse = statusResponse.body;
	if (applyResponseFailure(statusResponse, result)) return result;
	if (statusResponse.status < 200 || statusResponse.status >= 300) {
		result.failure = RequestFailure::Provider;
		result.error = "Hugging Face / fal-ai poll returned HTTP " +
			std::to_string(statusResponse.status);
		return result;
	}

	result.state = parseQueueState(extractStringField(statusResponse.body, "status"));
	if (result.state == MediaJobState::Queued || result.state == MediaJobState::Generating) {
		result.success = true;
		return result;
	}
	if (result.state == MediaJobState::Failed || result.state == MediaJobState::Cancelled) {
		result.failure = RequestFailure::Provider;
		result.error = extractStringField(statusResponse.body, "error");
		if (result.error.empty()) result.error = extractStringField(statusResponse.body, "message");
		if (result.error.empty()) result.error = "Hugging Face / fal-ai job did not complete";
		return result;
	}
	if (result.state != MediaJobState::Completed) {
		result.failure = RequestFailure::InvalidResponse;
		result.error = "Hugging Face / fal-ai job returned an unknown state";
		return result;
	}

	HttpRequest outputRequest;
	outputRequest.method = HttpMethod::Get;
	outputRequest.url = job.resultUrl;
	applyControl(outputRequest, control, 60);
	const HttpResponse outputResponse = endpoint.perform(std::move(outputRequest));
	result.rawResponse = outputResponse.body;
	if (applyResponseFailure(outputResponse, result)) return result;
	if (outputResponse.status < 200 || outputResponse.status >= 300) {
		if (result.failure == RequestFailure::None) result.failure = outputResponse.status > 0
			? RequestFailure::Provider : RequestFailure::Transport;
		result.error = outputResponse.error.empty()
			? "Hugging Face / fal-ai result is not available"
			: outputResponse.error;
		return result;
	}
	const std::string outputUrl = extractStringField(outputResponse.body, "url");
	if (outputUrl.empty()) {
		result.failure = RequestFailure::InvalidResponse;
		result.error = "Hugging Face / fal-ai result returned no output URL";
		return result;
	}
	result.resultUrl = outputUrl;
	downloadHuggingFaceFalOutput(outputUrl, result, control);
	return result;
}

bool MediaClient::downloadHuggingFaceFalOutput(
	const std::string & url, MediaJob & job, RequestControl control) const {
	HttpRequest downloadRequest;
	downloadRequest.method = HttpMethod::Get;
	downloadRequest.url = url;
	downloadRequest.accept = "*/*";
	downloadRequest.useBearerToken = false;
	applyControl(downloadRequest, control, 300);
	downloadRequest.maxResponseBytes = 512U * 1024U * 1024U;
	const HttpResponse response = endpoint.perform(std::move(downloadRequest));
	if (applyResponseFailure(response, job)) return false;
	if (response.status < 200 || response.status >= 300) {
		job.failure = RequestFailure::Provider;
		job.success = false;
		job.error = "media download returned HTTP " + std::to_string(response.status);
		return false;
	}
	if (response.body.empty()) {
		job.failure = RequestFailure::InvalidResponse;
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

} // namespace ofxIC
