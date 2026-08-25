#include "ofxICStabilityAudioClient.h"

#include "../endpoint/ofxICEndpoint.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

namespace ofxIC {
namespace {

constexpr std::size_t maxSubmitResponseBytes = 1024U * 1024U;
constexpr std::size_t maxAudioResponseBytes = 128U * 1024U * 1024U;

StabilityAudioJob failure(
	std::string message,
	int httpStatus = 0,
	std::string rawResponse = {}) {
	StabilityAudioJob result;
	result.httpStatus = httpStatus;
	result.state = StabilityAudioJobState::Failed;
	result.error = std::move(message);
	result.rawResponse = std::move(rawResponse);
	return result;
}

std::string responseFailure(
	const HttpResponse & response,
	const char * operation,
	const std::string & detail) {
	if (!response.error.empty()) return response.error;
	if (response.status > 0) {
		return std::string(operation) + " endpoint returned HTTP " +
			std::to_string(response.status) +
			(detail.empty() ? std::string{} : ": " + detail);
	}
	return std::string(operation) + " request failed before an HTTP response was received";
}

bool isHexJobId(const std::string & id) {
	return id.size() == 64U && std::all_of(id.begin(), id.end(), [](unsigned char c) {
		return std::isxdigit(c) != 0;
	});
}

std::string extractId(const std::string & json) {
	const std::size_t key = json.find("\"id\"");
	if (key == std::string::npos) return {};
	const std::size_t colon = json.find(':', key + 4U);
	if (colon == std::string::npos) return {};
	const std::size_t quote = json.find('"', colon + 1U);
	if (quote == std::string::npos) return {};
	const std::size_t end = json.find('"', quote + 1U);
	if (end == std::string::npos) return {};
	return json.substr(quote + 1U, end - quote - 1U);
}

void appendField(
	std::string & body,
	const std::string & boundary,
	const char * name,
	const std::string & value) {
	body += "--" + boundary + "\r\n";
	body += "Content-Disposition: form-data; name=\"" + std::string(name) + "\"\r\n\r\n";
	body += value;
	body += "\r\n";
}

std::string multipartBody(const StabilityAudioRequest & request, std::string & boundary) {
	static std::atomic<unsigned long long> sequence{ 0 };
	const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	boundary = "ofxIC-stability-audio-" + std::to_string(now) + "-" +
		std::to_string(sequence.fetch_add(1));
	while (request.prompt.find(boundary) != std::string::npos) boundary += "x";

	std::string body;
	appendField(body, boundary, "prompt", request.prompt);
	appendField(body, boundary, "model", request.model);
	appendField(body, boundary, "duration", std::to_string(request.durationSeconds));
	appendField(body, boundary, "seed", std::to_string(request.seed));
	appendField(body, boundary, "steps", std::to_string(request.steps));
	std::ostringstream guidance;
	guidance.imbue(std::locale::classic());
	guidance << request.guidance;
	appendField(body, boundary, "cfg_scale", guidance.str());
	appendField(body, boundary, "output_format", request.outputFormat);
	body += "--" + boundary + "\r\n";
	body += "Content-Disposition: form-data; name=\"none\"; filename=\"\"\r\n\r\n\r\n";
	body += "--" + boundary + "--\r\n";
	return body;
}

std::string validate(const StabilityAudioRequest & request) {
	if (request.prompt.empty()) return "music prompt is empty";
	if (request.prompt.size() > 10000U) return "music prompt exceeds the 10000-byte limit";
	if (request.model != "stable-audio-3") return "model must be stable-audio-3";
	if (request.durationSeconds < 1 || request.durationSeconds > 380) {
		return "duration must be between 1 and 380 seconds";
	}
	if (request.seed == std::numeric_limits<std::uint32_t>::max()) {
		return "seed must be between 0 and 4294967294";
	}
	if (request.steps < 4 || request.steps > 8) return "steps must be between 4 and 8";
	if (!std::isfinite(request.guidance) ||
		request.guidance < 1.0f || request.guidance > 25.0f) {
		return "guidance must be between 1 and 25";
	}
	if (request.outputFormat != "mp3" && request.outputFormat != "wav") {
		return "output format must be mp3 or wav";
	}
	return {};
}

bool hasExpectedAudioSignature(const std::string & bytes, const std::string & format) {
	if (format == "wav") {
		return bytes.size() >= 12U && bytes.compare(0, 4, "RIFF") == 0 &&
			bytes.compare(8, 4, "WAVE") == 0;
	}
	if (bytes.size() >= 3U && bytes.compare(0, 3, "ID3") == 0) return true;
	return bytes.size() >= 2U &&
		static_cast<unsigned char>(bytes[0]) == 0xffU &&
		(static_cast<unsigned char>(bytes[1]) & 0xe0U) == 0xe0U;
}

} // namespace

StabilityAudioClient::StabilityAudioClient(Endpoint & endpoint)
	: endpoint(endpoint) {}

StabilityAudioJob StabilityAudioClient::submit(const StabilityAudioRequest & request) const {
	const std::string validationError = validate(request);
	if (!validationError.empty()) return failure(validationError);

	std::string boundary;
	HttpRequest httpRequest;
	httpRequest.method = HttpMethod::Post;
	httpRequest.url = "/v2beta/audio/stable-audio/text-to-audio";
	httpRequest.body = multipartBody(request, boundary);
	httpRequest.contentType = "multipart/form-data; boundary=" + boundary;
	httpRequest.accept = "application/json";
	httpRequest.timeoutSeconds = 60;
	httpRequest.maxResponseBytes = maxSubmitResponseBytes;
	const HttpResponse response = endpoint.perform(std::move(httpRequest));
	if (response.status != 202) {
		return failure(responseFailure(
			response, "music", Endpoint::extractErrorText(response.body)),
			response.status, response.body);
	}

	const std::string id = extractId(response.body);
	if (!isHexJobId(id)) {
		return failure("music endpoint returned HTTP 202 without a valid job id", 202, response.body);
	}
	StabilityAudioJob result;
	result.success = true;
	result.httpStatus = 202;
	result.state = StabilityAudioJobState::Submitted;
	result.id = id;
	result.outputFormat = request.outputFormat;
	result.mimeType = request.outputFormat == "wav" ? "audio/wav" : "audio/mpeg";
	result.rawResponse = response.body;
	return result;
}

StabilityAudioJob StabilityAudioClient::poll(const StabilityAudioJob & job) const {
	if (!isHexJobId(job.id)) return failure("music job id must be 64 hexadecimal characters");
	if (job.outputFormat != "mp3" && job.outputFormat != "wav") {
		return failure("music job output format must be mp3 or wav");
	}
	HttpRequest request;
	request.method = HttpMethod::Get;
	request.url = "/v2beta/audio/results/" + job.id;
	request.accept = "audio/*";
	request.timeoutSeconds = 300;
	request.maxResponseBytes = maxAudioResponseBytes;
	const HttpResponse response = endpoint.perform(std::move(request));
	if (response.status == 202) {
		StabilityAudioJob result = job;
		result.success = true;
		result.httpStatus = 202;
		result.state = StabilityAudioJobState::Generating;
		result.audioBytes.clear();
		result.error.clear();
		result.rawResponse = response.body;
		return result;
	}
	if (response.status != 200) {
		StabilityAudioJob result = failure(
			responseFailure(
				response, "music result", Endpoint::extractErrorText(response.body)),
			response.status, response.body);
		result.id = job.id;
		result.outputFormat = job.outputFormat;
		result.mimeType = job.outputFormat == "wav" ? "audio/wav" : "audio/mpeg";
		return result;
	}
	if (!hasExpectedAudioSignature(response.body, job.outputFormat)) {
		StabilityAudioJob result = failure(
			"music result returned HTTP 200 without valid " + job.outputFormat + " audio", 200);
		result.id = job.id;
		result.outputFormat = job.outputFormat;
		result.mimeType = job.outputFormat == "wav" ? "audio/wav" : "audio/mpeg";
		return result;
	}
	StabilityAudioJob result = job;
	result.success = true;
	result.httpStatus = 200;
	result.state = StabilityAudioJobState::Completed;
	result.mimeType = job.outputFormat == "wav" ? "audio/wav" : "audio/mpeg";
	result.audioBytes = response.body;
	result.error.clear();
	result.rawResponse.clear();
	return result;
}

} // namespace ofxIC
