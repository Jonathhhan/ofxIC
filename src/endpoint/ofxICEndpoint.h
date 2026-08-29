#pragma once

#include "../chat/ofxICChatTypes.h"
#include "ofxICRequestTypes.h"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ofxIC {

class MediaClient;
class TranscriptionClient;
class SegmentationClient;
class StabilityAudioClient;
class AceStepMusicClient;

enum class HttpMethod {
	Get,
	Post
};

struct HttpRequest {
	HttpMethod method = HttpMethod::Get;
	std::string url;
	std::string body;
	std::string contentType = "application/json";
	std::string accept = "application/json";
	std::vector<std::pair<std::string, std::string>> headers;
	int timeoutSeconds = 180;
	std::size_t maxResponseBytes = 64U * 1024U * 1024U;
	bool useBearerToken = true;
	bool stream = false;
	ChatChunkCallback onChunk;
	std::function<bool()> shouldCancel;
};

struct HttpResponse {
	bool started = false;
	bool cancelled = false;
	RequestFailure failure = RequestFailure::None;
	int status = 0;
	std::string body;
	std::string streamedText;
	std::string error;
};

using HttpTransport = std::function<HttpResponse(const HttpRequest &)>;

struct EndpointStatus {
	bool reachable = false;
	bool cancelled = false;
	RequestFailure failure = RequestFailure::None;
	int httpStatus = 0;
	std::vector<std::string> models;
	std::string error;

	explicit operator bool() const {
		return reachable;
	}
};

struct EndpointUrlValidation {
	bool valid = false;
	bool secure = false;
	bool loopback = false;
	std::string normalizedUrl;
	std::string error;

	explicit operator bool() const { return valid; }
};

class Endpoint {
public:
	explicit Endpoint(
		std::string baseUrl = "http://127.0.0.1:8080",
		HttpTransport transport = {});

	void setBaseUrl(std::string baseUrl);
	const std::string & getBaseUrl() const;

	static EndpointUrlValidation validateBaseUrl(const std::string & baseUrl);

	void setBearerToken(std::string token);
	bool hasBearerToken() const;

	EndpointStatus inspect(RequestControl control = {}) const;
	EndpointStatus inspect(std::function<bool()> shouldCancel) const;
	ChatResult chat(
		const ChatRequest & request,
		ChatChunkCallback onChunk = nullptr,
		RequestControl control = {}) const;
	ChatResult chat(const ChatRequest & request, ChatChunkCallback onChunk,
		std::function<bool()> shouldCancel) const;

private:
	HttpResponse perform(HttpRequest request) const;
	static std::string normalizeBaseUrl(const std::string & baseUrl);
	static std::string buildChatBody(const ChatRequest & request);
	static std::string extractChatText(const std::string & responseBody);
	static std::string extractErrorText(const std::string & responseBody);
	static std::vector<ToolCall> extractToolCalls(const std::string & responseBody);
	static std::vector<std::string> extractModelIds(const std::string & responseBody);
	static HttpResponse runHttpRequest(const HttpRequest & request);

	std::string baseUrl;
	std::string bearerToken;
	HttpTransport transport;

	friend class MediaClient;
	friend class TranscriptionClient;
	friend class SegmentationClient;
	friend class StabilityAudioClient;
	friend class AceStepMusicClient;
};

} // namespace ofxIC
