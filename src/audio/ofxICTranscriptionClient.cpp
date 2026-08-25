#include "ofxICTranscriptionClient.h"

#include <algorithm>
#include <utility>

namespace ofxIC {
namespace {

constexpr std::size_t kOpenAIMaxAudioBytes = 25U * 1024U * 1024U;

std::string safeFilename(std::string value) {
	value.erase(std::remove_if(value.begin(), value.end(), [](char c) {
		return c == '\r' || c == '\n' || c == '"' || c == '\\';
	}), value.end());
	return value.empty() ? "audio.wav" : value;
}

void appendField(std::string & body, const std::string & boundary,
	const std::string & name, const std::string & value) {
	if (value.empty()) return;
	body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" +
		name + "\"\r\n\r\n" + value + "\r\n";
}

std::string buildMultipart(const TranscriptionRequest & request,
	bool includeModel, std::string & boundary) {
	boundary = "----ofxICTranscriptionBoundary";
	while (request.audioBytes.find(boundary) != std::string::npos) boundary += "x";
	std::string body;
	body.reserve(request.audioBytes.size() + 1024);
	body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" +
		safeFilename(request.filename) + "\"\r\nContent-Type: " +
		(request.contentType.empty() ? "application/octet-stream" : request.contentType) +
		"\r\n\r\n";
	body.append(request.audioBytes);
	body += "\r\n";
	if (includeModel) appendField(body, boundary, "model", request.model);
	appendField(body, boundary, "language", request.language);
	appendField(body, boundary, "prompt", request.prompt);
	appendField(body, boundary, "response_format", "json");
	body += "--" + boundary + "--\r\n";
	return body;
}

} // namespace

TranscriptionClient::TranscriptionClient(Endpoint & endpoint) : endpoint(endpoint) {}

TranscriptionResult TranscriptionClient::transcribeOpenAI(
	const TranscriptionRequest & request,
	std::function<bool()> shouldCancel) const {
	if (request.audioBytes.size() > kOpenAIMaxAudioBytes) {
		TranscriptionResult result;
		result.error = "OpenAI transcription files are limited to 25 MB; loaded file has " +
			std::to_string(request.audioBytes.size()) + " bytes";
		return result;
	}
	return transcribe(request, "/v1/audio/transcriptions", true, std::move(shouldCancel));
}

TranscriptionResult TranscriptionClient::transcribeWhisperCpp(
	const TranscriptionRequest & request,
	std::function<bool()> shouldCancel) const {
	return transcribe(request, "/inference", false, std::move(shouldCancel));
}

TranscriptionResult TranscriptionClient::transcribe(
	const TranscriptionRequest & request,
	const std::string & path,
	bool includeModel,
	std::function<bool()> shouldCancel) const {
	TranscriptionResult result;
	if (request.audioBytes.empty()) {
		result.error = "transcription audio is empty";
		return result;
	}
	std::string boundary;
	HttpRequest httpRequest;
	httpRequest.method = HttpMethod::Post;
	httpRequest.url = path;
	httpRequest.body = buildMultipart(request, includeModel, boundary);
	httpRequest.contentType = "multipart/form-data; boundary=" + boundary;
	httpRequest.timeoutSeconds = 300;
	httpRequest.maxResponseBytes = 16U * 1024U * 1024U;
	httpRequest.shouldCancel = std::move(shouldCancel);
	const HttpResponse response = endpoint.perform(std::move(httpRequest));
	result.httpStatus = response.status;
	result.cancelled = response.cancelled;
	result.rawResponse = response.body;
	if (response.cancelled) {
		result.error = response.error.empty() ? "request cancelled" : response.error;
		return result;
	}
	if (!response.started) {
		result.error = response.error.empty() ? "transcription request did not start" : response.error;
		return result;
	}
	if (response.status < 200 || response.status >= 300) {
		result.error = "transcription endpoint returned HTTP " + std::to_string(response.status);
		const std::string detail = Endpoint::extractErrorText(response.body);
		if (!detail.empty()) result.error += ": " + detail;
		else if (!response.error.empty()) result.error += ": " + response.error;
		return result;
	}
	result.text = Endpoint::extractChatText(response.body);
	if (result.text.empty()) {
		result.error = "transcription endpoint returned no text";
		return result;
	}
	result.success = true;
	return result;
}

} // namespace ofxIC
