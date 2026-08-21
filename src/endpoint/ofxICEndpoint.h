#pragma once

#include "../chat/ofxICChatTypes.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ofxIC {

class MediaClient;

enum class HttpMethod {
	Get,
	Post
};

struct HttpRequest {
	HttpMethod method = HttpMethod::Get;
	std::string url;
	std::string body;
	std::string contentType = "application/json";
	std::vector<std::pair<std::string, std::string>> headers;
	int timeoutSeconds = 180;
	bool stream = false;
	ChatChunkCallback onChunk;
	std::function<bool()> shouldCancel;
};

struct HttpResponse {
	bool started = false;
	bool cancelled = false;
	int status = 0;
	std::string body;
	std::string streamedText;
	std::string error;
};

using HttpTransport = std::function<HttpResponse(const HttpRequest &)>;

struct EndpointStatus {
	bool reachable = false;
	int httpStatus = 0;
	std::vector<std::string> models;
	std::string error;

	explicit operator bool() const {
		return reachable;
	}
};

class Endpoint {
public:
	explicit Endpoint(
		std::string baseUrl = "http://127.0.0.1:8080",
		HttpTransport transport = {});

	void setBaseUrl(std::string baseUrl);
	const std::string & getBaseUrl() const;

	void setBearerToken(std::string token);
	bool hasBearerToken() const;

	EndpointStatus inspect() const;
	ChatResult chat(
		const ChatRequest & request,
		ChatChunkCallback onChunk = nullptr) const;

private:
	HttpResponse perform(HttpRequest request) const;
	static std::string normalizeBaseUrl(const std::string & baseUrl);
	static std::string buildChatBody(const ChatRequest & request);
	static std::string extractChatText(const std::string & responseBody);
	static std::vector<ToolCall> extractToolCalls(const std::string & responseBody);
	static std::vector<std::string> extractModelIds(const std::string & responseBody);
	static HttpResponse runHttpRequest(const HttpRequest & request);

	std::string baseUrl;
	std::string bearerToken;
	HttpTransport transport;

	friend class MediaClient;
};

} // namespace ofxIC
