#include "ofxICEndpoint.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <locale>
#include <sstream>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "Winhttp.lib")
#endif

#if __has_include("ofMain.h")
#include "ofMain.h"
#define OFXIC_HAS_OF_HTTP_RUNTIME 1
#endif

#if defined(OFXIC_HAS_OF_HTTP_RUNTIME) && __has_include("curl/curl.h")
#if defined(_WIN32) && !defined(CURL_STATICLIB)
#define CURL_STATICLIB
#endif
#include "curl/curl.h"
#define OFXIC_HAS_CURL_HTTP_RUNTIME 1
#endif

namespace ofxIC {
namespace {

#if defined(_WIN32)
std::wstring widen(const std::string & value) {
	if (value.empty()) return {};
	const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
	std::wstring result(size, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
	return result;
}

std::string winHttpError(const char * operation) {
	return std::string(operation) + " failed with Windows error " + std::to_string(GetLastError());
}

struct WinHttpHandle {
	HINTERNET value = nullptr;
	~WinHttpHandle() { if (value) WinHttpCloseHandle(value); }
};

struct WinHttpRequestHandle {
	std::atomic<HINTERNET> value{ nullptr };
	explicit WinHttpRequestHandle(HINTERNET handle) : value(handle) {}
	~WinHttpRequestHandle() {
		const HINTERNET handle = value.exchange(nullptr);
		if (handle) WinHttpCloseHandle(handle);
	}
	HINTERNET get() const { return value.load(); }
};

class WinHttpCancellation {
public:
	WinHttpCancellation(
		WinHttpRequestHandle & requestHandle,
		std::function<bool()> shouldCancel)
		: requestHandle(requestHandle)
		, shouldCancel(std::move(shouldCancel)) {
		if (!this->shouldCancel) return;
		watcher = std::thread([this]() {
			while (!finished.load()) {
				if (this->shouldCancel()) {
					cancelled = true;
					const HINTERNET handle = this->requestHandle.value.exchange(nullptr);
					if (handle) WinHttpCloseHandle(handle);
					return;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		});
	}

	~WinHttpCancellation() {
		finished = true;
		if (watcher.joinable()) watcher.join();
	}

	bool wasCancelled() const { return cancelled.load(); }

private:
	WinHttpRequestHandle & requestHandle;
	std::function<bool()> shouldCancel;
	std::atomic<bool> finished{ false };
	std::atomic<bool> cancelled{ false };
	std::thread watcher;
};

HttpResponse runWinHttpRequest(const HttpRequest & request) {
	HttpResponse result;
	auto cancelled = [&]() {
		if (!request.shouldCancel || !request.shouldCancel()) return false;
		result.cancelled = true;
		result.failure = RequestFailure::Cancelled;
		result.error = "request cancelled";
		return true;
	};
	if (cancelled()) return result;
	const std::wstring url = widen(request.url);
	URL_COMPONENTS parts{};
	parts.dwStructSize = sizeof(parts);
	parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
	if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
		result.error = winHttpError("WinHttpCrackUrl");
		return result;
	}
	const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
	std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
	path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
	WinHttpHandle session{ WinHttpOpen(L"ofxIC/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
	if (!session.value) { result.error = winHttpError("WinHttpOpen"); return result; }
	const int timeout = std::max(1, request.timeoutSeconds) * 1000;
	WinHttpSetTimeouts(session.value, timeout, timeout, timeout, timeout);
	WinHttpHandle connection{ WinHttpConnect(session.value, host.c_str(), parts.nPort, 0) };
	if (!connection.value) { result.error = winHttpError("WinHttpConnect"); return result; }
	const wchar_t * method = request.method == HttpMethod::Post ? L"POST" : L"GET";
	WinHttpRequestHandle operation{ WinHttpOpenRequest(connection.value, method, path.c_str(), nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
		parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) };
	if (!operation.get()) { result.error = winHttpError("WinHttpOpenRequest"); return result; }
	WinHttpCancellation cancellation(operation, request.shouldCancel);
	auto operationFailed = [&](const char * name) {
		const DWORD error = GetLastError();
		if (cancellation.wasCancelled()) {
			result.cancelled = true;
			result.failure = RequestFailure::Cancelled;
			result.error = "request cancelled";
		} else if (error == ERROR_WINHTTP_TIMEOUT) {
			result.failure = RequestFailure::Timeout;
			result.error = "request timed out";
		} else {
			result.failure = RequestFailure::Transport;
			result.error = std::string(name) + " failed with Windows error " +
				std::to_string(error);
		}
	};
	std::wstring headers = L"Accept: " + widen(request.accept) + L"\r\nContent-Type: " + widen(request.contentType) + L"\r\n";
	for (const auto & header : request.headers) headers += widen(header.first + ": " + header.second) + L"\r\n";
	if (cancelled()) return result;
	void * body = request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char *>(request.body.data());
	const DWORD bodySize = static_cast<DWORD>(request.body.size());
	result.started = true;
	if (!WinHttpSendRequest(operation.get(), headers.c_str(), static_cast<DWORD>(-1), body, bodySize, bodySize, 0)) {
		operationFailed("WinHttpSendRequest"); return result;
	}
	if (cancelled()) return result;
	if (!WinHttpReceiveResponse(operation.get(), nullptr)) {
		operationFailed("WinHttpReceiveResponse"); return result;
	}
	DWORD status = 0, statusSize = sizeof(status);
	WinHttpQueryHeaders(operation.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
	result.status = static_cast<int>(status);
	while (!cancelled()) {
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(operation.get(), &available)) { operationFailed("WinHttpQueryDataAvailable"); break; }
		if (available == 0) break;
		const std::size_t offset = result.body.size();
		if (available > request.maxResponseBytes ||
			offset > request.maxResponseBytes - available) {
			result.status = 0;
			result.body.clear();
			result.error = "response exceeded " +
				std::to_string(request.maxResponseBytes) + " byte limit";
			break;
		}
		result.body.resize(offset + available);
		DWORD received = 0;
		if (!WinHttpReadData(operation.get(), result.body.data() + offset, available, &received)) {
			operationFailed("WinHttpReadData"); result.body.resize(offset); break;
		}
		result.body.resize(offset + received);
	}
	return result;
}
#endif

std::string trimCopy(const std::string & value) {
	std::size_t first = 0;
	while (first < value.size() &&
		std::isspace(static_cast<unsigned char>(value[first]))) {
		++first;
	}
	std::size_t last = value.size();
	while (last > first &&
		std::isspace(static_cast<unsigned char>(value[last - 1]))) {
		--last;
	}
	return value.substr(first, last - first);
}

bool endsWith(const std::string & value, const std::string & suffix) {
	return value.size() >= suffix.size() &&
		value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string stripTrailingSlash(std::string value) {
	while (!value.empty() && value.back() == '/') {
		value.pop_back();
	}
	return value;
}

std::string configuredCaBundle() {
	for (const char * variable : { "OFXIC_CA_BUNDLE", "CURL_CA_BUNDLE", "SSL_CERT_FILE" }) {
#if defined(_WIN32)
		char * value = nullptr;
		std::size_t size = 0;
		_dupenv_s(&value, &size, variable);
		const std::string configured = value ? trimCopy(value) : std::string{};
		std::free(value);
		if (!configured.empty()) return configured;
#else
		const char * value = std::getenv(variable);
		if (value && *value) return trimCopy(value);
#endif
	}
	return {};
}

const char * roleLabel(ChatRole role) {
	switch (role) {
	case ChatRole::System: return "system";
	case ChatRole::User: return "user";
	case ChatRole::Assistant: return "assistant";
	case ChatRole::Tool: return "tool";
	}
	return "user";
}

std::string escapeJson(const std::string & value) {
	std::ostringstream escaped;
	for (const unsigned char c : value) {
		switch (c) {
		case '\\': escaped << "\\\\"; break;
		case '"': escaped << "\\\""; break;
		case '\b': escaped << "\\b"; break;
		case '\f': escaped << "\\f"; break;
		case '\n': escaped << "\\n"; break;
		case '\r': escaped << "\\r"; break;
		case '\t': escaped << "\\t"; break;
		default:
			if (c < 0x20) {
				const char * hex = "0123456789abcdef";
				escaped << "\\u00" << hex[(c >> 4) & 0x0f] << hex[c & 0x0f];
			} else {
				escaped << static_cast<char>(c);
			}
			break;
		}
	}
	return escaped.str();
}

int hexValue(char value) {
	if (value >= '0' && value <= '9') return value - '0';
	if (value >= 'a' && value <= 'f') return 10 + value - 'a';
	if (value >= 'A' && value <= 'F') return 10 + value - 'A';
	return -1;
}

bool readHexCodeUnit(const std::string & value, std::size_t & index, unsigned int & codeUnit) {
	if (index + 4 > value.size()) return false;
	codeUnit = 0;
	for (int i = 0; i < 4; ++i) {
		const int digit = hexValue(value[index++]);
		if (digit < 0) return false;
		codeUnit = (codeUnit << 4U) | static_cast<unsigned int>(digit);
	}
	return true;
}

void appendUtf8(unsigned int codePoint, std::string & output) {
	if (codePoint <= 0x7fU) {
		output.push_back(static_cast<char>(codePoint));
	} else if (codePoint <= 0x7ffU) {
		output.push_back(static_cast<char>(0xc0U | (codePoint >> 6U)));
		output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
	} else if (codePoint <= 0xffffU) {
		output.push_back(static_cast<char>(0xe0U | (codePoint >> 12U)));
		output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
		output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
	} else {
		output.push_back(static_cast<char>(0xf0U | (codePoint >> 18U)));
		output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3fU)));
		output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
		output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
	}
}

bool appendDecodedJsonChar(
	const std::string & value,
	std::size_t & index,
	std::string & output) {
	if (index >= value.size()) {
		return false;
	}
	const char c = value[index++];
	if (c != '\\') {
		output.push_back(c);
		return true;
	}
	if (index >= value.size()) {
		return false;
	}
	const char escaped = value[index++];
	switch (escaped) {
	case '"': output.push_back('"'); return true;
	case '\\': output.push_back('\\'); return true;
	case '/': output.push_back('/'); return true;
	case 'b': output.push_back('\b'); return true;
	case 'f': output.push_back('\f'); return true;
	case 'n': output.push_back('\n'); return true;
	case 'r': output.push_back('\r'); return true;
	case 't': output.push_back('\t'); return true;
	case 'u': {
		unsigned int codePoint = 0;
		if (!readHexCodeUnit(value, index, codePoint)) return false;
		if (codePoint >= 0xd800U && codePoint <= 0xdbffU) {
			if (index + 6 > value.size() || value[index] != '\\' || value[index + 1] != 'u') {
				return false;
			}
			index += 2;
			unsigned int low = 0;
			if (!readHexCodeUnit(value, index, low) || low < 0xdc00U || low > 0xdfffU) {
				return false;
			}
			codePoint = 0x10000U + ((codePoint - 0xd800U) << 10U) + (low - 0xdc00U);
		}
		appendUtf8(codePoint, output);
		return true;
	}
	default:
		return false;
	}
}

std::string extractJsonStringAt(
	const std::string & json,
	std::size_t valueStart) {
	while (valueStart < json.size() &&
		std::isspace(static_cast<unsigned char>(json[valueStart]))) {
		++valueStart;
	}
	if (valueStart >= json.size() || json[valueStart] != '"') {
		return {};
	}
	++valueStart;
	std::string decoded;
	while (valueStart < json.size()) {
		if (json[valueStart] == '"') {
			return decoded;
		}
		if (!appendDecodedJsonChar(json, valueStart, decoded)) {
			return {};
		}
	}
	return {};
}

std::string extractJsonStringField(
	const std::string & json,
	const std::string & key,
	std::size_t searchFrom = 0) {
	const std::string quotedKey = "\"" + key + "\"";
	const std::size_t keyPosition = json.find(quotedKey, searchFrom);
	if (keyPosition == std::string::npos) {
		return {};
	}
	const std::size_t colon = json.find(':', keyPosition + quotedKey.size());
	if (colon == std::string::npos) {
		return {};
	}
	return extractJsonStringAt(json, colon + 1);
}

std::size_t findMatchingJsonDelimiter(
	const std::string & json,
	std::size_t openPosition,
	char openDelimiter,
	char closeDelimiter) {
	if (openPosition >= json.size() || json[openPosition] != openDelimiter) return std::string::npos;
	int depth = 0;
	bool inString = false;
	bool escaped = false;
	for (std::size_t i = openPosition; i < json.size(); ++i) {
		const char c = json[i];
		if (inString) {
			if (escaped) escaped = false;
			else if (c == '\\') escaped = true;
			else if (c == '"') inString = false;
			continue;
		}
		if (c == '"') inString = true;
		else if (c == openDelimiter) ++depth;
		else if (c == closeDelimiter && --depth == 0) return i;
	}
	return std::string::npos;
}

std::string extractScopedJsonStringField(
	const std::string & json,
	const std::string & key,
	std::size_t begin,
	std::size_t end) {
	const std::string quotedKey = "\"" + key + "\"";
	const std::size_t keyPosition = json.find(quotedKey, begin);
	if (keyPosition == std::string::npos || keyPosition >= end) return {};
	const std::size_t colon = json.find(':', keyPosition + quotedKey.size());
	if (colon == std::string::npos || colon >= end) return {};
	return extractJsonStringAt(json, colon + 1);
}

std::string extractChatTextValue(const std::string & responseBody) {
	for (const char * keyValue : { "content", "text", "response" }) {
		const std::string key(keyValue);
		const std::string value = extractJsonStringField(responseBody, key);
		if (!trimCopy(value).empty()) {
			return value;
		}
	}
	return {};
}

ToolCall extractTextSerializedToolCall(const std::string & text) {
	const std::size_t nameKey = text.find("\"name\"");
	const std::size_t argumentsKey = text.find("\"arguments\"", nameKey);
	if (nameKey == std::string::npos || argumentsKey == std::string::npos) return {};
	const std::size_t objectStart = text.rfind('{', nameKey);
	if (objectStart == std::string::npos) return {};
	const std::size_t objectEnd = findMatchingJsonDelimiter(text, objectStart, '{', '}');
	if (objectEnd == std::string::npos || argumentsKey >= objectEnd) return {};
	const std::string name = extractScopedJsonStringField(
		text, "name", objectStart, objectEnd);
	const std::size_t argumentsColon = text.find(':', argumentsKey + 11);
	const std::size_t argumentsStart = text.find('{', argumentsColon);
	if (name.empty() || argumentsColon == std::string::npos ||
		argumentsStart == std::string::npos || argumentsStart >= objectEnd) return {};
	const std::size_t argumentsEnd = findMatchingJsonDelimiter(
		text, argumentsStart, '{', '}');
	if (argumentsEnd == std::string::npos || argumentsEnd > objectEnd) return {};
	ToolCall call;
	call.id = "llama-text-call-1";
	call.name = name;
	call.argumentsJson = text.substr(argumentsStart, argumentsEnd - argumentsStart + 1);
	return call;
}

#if defined(OFXIC_HAS_CURL_HTTP_RUNTIME)
bool processServerSentEventLine(
	const std::string & line,
	HttpResponse & response,
	const ChatChunkCallback & onChunk) {
	const std::string prefix = "data:";
	if (line.compare(0, prefix.size(), prefix) != 0) {
		return true;
	}
	const std::string payload = trimCopy(line.substr(prefix.size()));
	if (payload.empty() || payload == "[DONE]") {
		return true;
	}
	response.body += payload + "\n";
	const std::string text = extractChatTextValue(payload);
	if (text.empty()) {
		return true;
	}
	response.streamedText += text;
	if (onChunk && !onChunk(text)) {
		response.cancelled = true;
		response.failure = RequestFailure::Cancelled;
		response.error = "request cancelled";
		return false;
	}
	return true;
}

struct CurlState {
	HttpResponse * response = nullptr;
	ChatChunkCallback onChunk;
	std::function<bool()> shouldCancel;
	bool streaming = false;
	std::size_t maxResponseBytes = 0;
	std::string pending;
};

bool shouldCancel(CurlState & state) {
	if (!state.shouldCancel || !state.shouldCancel()) {
		return false;
	}
	state.response->cancelled = true;
	state.response->failure = RequestFailure::Cancelled;
	state.response->error = "request cancelled";
	return true;
}

int curlProgress(void * userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
	auto * state = static_cast<CurlState *>(userData);
	return state && shouldCancel(*state) ? 1 : 0;
}

std::size_t curlWrite(
	char * data,
	std::size_t size,
	std::size_t count,
	void * userData) {
	const std::size_t bytes = size * count;
	auto * state = static_cast<CurlState *>(userData);
	if (!state || !state->response || !data || shouldCancel(*state)) {
		return 0;
	}
	const std::size_t buffered = state->response->body.size() +
		(state->streaming ? state->pending.size() : 0U);
	if (bytes > state->maxResponseBytes ||
		buffered > state->maxResponseBytes - bytes) {
		state->response->body.clear();
		state->response->error = "response exceeded " +
			std::to_string(state->maxResponseBytes) + " byte limit";
		return 0;
	}
	state->response->started = true;
	if (!state->streaming) {
		state->response->body.append(data, bytes);
		return bytes;
	}
	state->pending.append(data, bytes);
	while (true) {
		const std::size_t newline = state->pending.find('\n');
		if (newline == std::string::npos) {
			break;
		}
		std::string line = state->pending.substr(0, newline);
		state->pending.erase(0, newline + 1);
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (!processServerSentEventLine(line, *state->response, state->onChunk)) {
			return 0;
		}
	}
	return bytes;
}

HttpResponse runCurlRequest(const HttpRequest & request) {
	HttpResponse result;
	CURL * curl = curl_easy_init();
	if (!curl) {
		result.error = "curl initialization failed";
		return result;
	}

	struct curl_slist * headers = nullptr;
	const std::string accept = "Accept: " +
		(request.stream ? std::string("text/event-stream") : request.accept);
	headers = curl_slist_append(headers, accept.c_str());
	const std::string contentType = "Content-Type: " + request.contentType;
	headers = curl_slist_append(headers, contentType.c_str());
	for (const auto & header : request.headers) {
		const std::string value = header.first + ": " + header.second;
		headers = curl_slist_append(headers, value.c_str());
	}

	CurlState state;
	char errorBuffer[CURL_ERROR_SIZE] = {};
	state.response = &result;
	state.onChunk = request.onChunk;
	state.shouldCancel = request.shouldCancel;
	state.streaming = request.stream;
	state.maxResponseBytes = request.maxResponseBytes;

	curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
	if (request.method == HttpMethod::Post) {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
	}
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(request.timeoutSeconds));
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "ofxIC/0.1");
	const std::string caBundle = configuredCaBundle();
	if (!caBundle.empty()) {
		curl_easy_setopt(curl, CURLOPT_CAINFO, caBundle.c_str());
	}
#if defined(_WIN32)
	else {
		curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
	}
#endif
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);

	const CURLcode code = curl_easy_perform(curl);
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	result.status = static_cast<int>(status);
	result.started = true;
	if (code != CURLE_OK && !result.cancelled && result.error.empty()) {
		result.failure = code == CURLE_OPERATION_TIMEDOUT
			? RequestFailure::Timeout
			: RequestFailure::Transport;
		result.error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(code);
	}
	if (!result.error.empty() && !result.cancelled) result.status = 0;
	if (request.stream && !state.pending.empty() && !result.cancelled) {
		processServerSentEventLine(state.pending, result, request.onChunk);
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return result;
}
#endif

} // namespace

Endpoint::Endpoint(std::string baseUrl, HttpTransport transport)
	: baseUrl(normalizeBaseUrl(trimCopy(baseUrl).empty()
		? "http://127.0.0.1:8080" : baseUrl))
	, transport(transport ? std::move(transport) : Endpoint::runHttpRequest) {
}

void Endpoint::setBaseUrl(std::string baseUrl) {
	this->baseUrl = normalizeBaseUrl(baseUrl);
}

const std::string & Endpoint::getBaseUrl() const {
	return baseUrl;
}

EndpointUrlValidation Endpoint::validateBaseUrl(const std::string & value) {
	EndpointUrlValidation result;
	const std::string trimmed = trimCopy(value);
	if (trimmed.empty()) {
		result.error = "base URL is empty";
		return result;
	}
	if (trimmed.size() > 2048) {
		result.error = "base URL exceeds 2048 characters";
		return result;
	}
	for (const unsigned char character : trimmed) {
		if (character <= 0x20 || character == 0x7f || character == '\\') {
			result.error = "base URL contains whitespace, control characters, or backslashes";
			return result;
		}
	}
	result.normalizedUrl = normalizeBaseUrl(trimmed);
	std::size_t authorityStart = 0;
	if (result.normalizedUrl.compare(0, 8, "https://") == 0) {
		result.secure = true;
		authorityStart = 8;
	} else if (result.normalizedUrl.compare(0, 7, "http://") == 0) {
		authorityStart = 7;
	} else {
		result.error = "base URL must begin with lowercase http:// or https://";
		return result;
	}
	if (result.normalizedUrl.find_first_of("?#", authorityStart) != std::string::npos) {
		result.error = "base URL must not contain a query string or fragment";
		return result;
	}
	const std::size_t authorityEnd = result.normalizedUrl.find('/', authorityStart);
	const std::string authority = result.normalizedUrl.substr(authorityStart,
		authorityEnd == std::string::npos ? std::string::npos : authorityEnd - authorityStart);
	if (authority.empty()) {
		result.error = "base URL host is empty";
		return result;
	}
	if (authority.find('@') != std::string::npos) {
		result.error = "base URL must not embed user names or credentials";
		return result;
	}

	std::string host;
	std::string port;
	if (authority.front() == '[') {
		const std::size_t closingBracket = authority.find(']');
		if (closingBracket == std::string::npos || closingBracket == 1) {
			result.error = "base URL contains an invalid bracketed IPv6 host";
			return result;
		}
		host = authority.substr(1, closingBracket - 1);
		const std::string remainder = authority.substr(closingBracket + 1);
		if (!remainder.empty()) {
			if (remainder.front() != ':' || remainder.size() == 1) {
				result.error = "base URL contains an invalid IPv6 port";
				return result;
			}
			port = remainder.substr(1);
		}
	} else {
		const std::size_t colon = authority.rfind(':');
		if (colon != std::string::npos) {
			if (authority.find(':') != colon) {
				result.error = "IPv6 hosts in base URLs must use brackets";
				return result;
			}
			host = authority.substr(0, colon);
			port = authority.substr(colon + 1);
		} else {
			host = authority;
		}
	}
	if (host.empty()) {
		result.error = "base URL host is empty";
		return result;
	}
	for (const unsigned char character : host) {
		if (!(std::isalnum(character) || character == '.' || character == '-' ||
			character == ':' || character == '_')) {
			result.error = "base URL host contains unsupported characters";
			return result;
		}
	}
	if (!port.empty()) {
		int parsedPort = 0;
		for (const unsigned char character : port) {
			if (!std::isdigit(character)) {
				result.error = "base URL port must be numeric";
				return result;
			}
			parsedPort = parsedPort * 10 + (character - '0');
			if (parsedPort > 65535) break;
		}
		if (parsedPort < 1 || parsedPort > 65535) {
			result.error = "base URL port must be between 1 and 65535";
			return result;
		}
	}
	std::string lowerHost = host;
	std::transform(lowerHost.begin(), lowerHost.end(), lowerHost.begin(),
		[](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
	result.loopback = lowerHost == "localhost" || lowerHost == "::1" ||
		lowerHost.compare(0, 4, "127.") == 0;
	result.valid = true;
	return result;
}

void Endpoint::setBearerToken(std::string token) {
	bearerToken = trimCopy(token);
}

bool Endpoint::hasBearerToken() const {
	return !bearerToken.empty();
}

HttpResponse Endpoint::perform(HttpRequest request) const {
	const EndpointUrlValidation validation = validateBaseUrl(baseUrl);
	if (!validation) {
		HttpResponse response;
		response.failure = RequestFailure::Validation;
		response.error = "invalid endpoint configuration: " + validation.error;
		return response;
	}
	if (request.url.compare(0, 7, "http://") != 0 &&
		request.url.compare(0, 8, "https://") != 0) {
		if (request.url.empty() || request.url.front() != '/') {
			request.url.insert(request.url.begin(), '/');
		}
		request.url = baseUrl + request.url;
	}
	if (request.useBearerToken && !bearerToken.empty()) {
		const auto authorization = std::find_if(
			request.headers.begin(),
			request.headers.end(),
			[](const std::pair<std::string, std::string> & header) {
				return header.first == "Authorization";
			});
		if (authorization == request.headers.end()) {
			request.headers.emplace_back("Authorization", "Bearer " + bearerToken);
		}
	}
	HttpResponse response = transport(request);
	if (response.body.size() > request.maxResponseBytes) {
		response.status = 0;
		response.body.clear();
		response.failure = RequestFailure::InvalidResponse;
		response.error = "response exceeded " +
			std::to_string(request.maxResponseBytes) + " byte limit";
	}
	return response;
}

EndpointStatus Endpoint::inspect(std::function<bool()> shouldCancel) const {
	RequestControl control;
	control.shouldCancel = std::move(shouldCancel);
	return inspect(std::move(control));
}

EndpointStatus Endpoint::inspect(RequestControl control) const {
	EndpointStatus status;
	if (control.timeoutSeconds < 0) {
		status.failure = RequestFailure::InvalidResponse;
		status.error = "request timeout cannot be negative";
		return status;
	}
	HttpRequest request;
	request.method = HttpMethod::Get;
	request.url = "/v1/models";
	request.timeoutSeconds = control.timeoutSeconds > 0 ? control.timeoutSeconds : 10;
	request.maxResponseBytes = 8U * 1024U * 1024U;
	request.shouldCancel = std::move(control.shouldCancel);
	const HttpResponse response = perform(request);
	status.httpStatus = response.status;
	status.cancelled = response.cancelled;
	status.failure = response.failure;
	if (response.cancelled) {
		status.failure = RequestFailure::Cancelled;
		status.error = response.error.empty() ? "request cancelled" : response.error;
		return status;
	}
	if (!response.started) {
		if (status.failure == RequestFailure::None) status.failure = RequestFailure::Transport;
		status.error = response.error.empty() ? "request did not start" : response.error;
		return status;
	}
	if (response.status <= 0) {
		if (status.failure == RequestFailure::None) status.failure = RequestFailure::Transport;
		status.error = response.error.empty() ? "model request failed" : response.error;
		return status;
	}
	if (response.status < 200 || response.status >= 300) {
		status.failure = RequestFailure::Provider;
		status.error = "model endpoint returned HTTP " + std::to_string(response.status);
		if (!response.error.empty()) {
			status.error += ": " + response.error;
		}
		return status;
	}
	status.reachable = true;
	status.models = extractModelIds(response.body);
	return status;
}

ChatResult Endpoint::chat(
	const ChatRequest & request,
	ChatChunkCallback onChunk,
	std::function<bool()> shouldCancel) const {
	RequestControl control;
	control.shouldCancel = std::move(shouldCancel);
	return chat(request, std::move(onChunk), std::move(control));
}

ChatResult Endpoint::chat(
	const ChatRequest & request,
	ChatChunkCallback onChunk,
	RequestControl control) const {
	ChatResult result;
	if (control.timeoutSeconds < 0) {
		result.failure = RequestFailure::InvalidResponse;
		result.error = "request timeout cannot be negative";
		return result;
	}
	if (request.messages.empty()) {
		result.failure = RequestFailure::InvalidResponse;
		result.error = "chat request has no messages";
		return result;
	}
	if (request.options.stream && !request.tools.empty()) {
		result.failure = RequestFailure::InvalidResponse;
		result.error = "streaming tool calls are not supported yet";
		return result;
	}
	if (!std::isfinite(request.options.temperature) || !std::isfinite(request.options.topP)) {
		result.failure = RequestFailure::Validation;
		result.error = "chat temperature and top_p must be finite numbers";
		return result;
	}

	HttpRequest httpRequest;
	httpRequest.method = HttpMethod::Post;
	httpRequest.maxResponseBytes = 16U * 1024U * 1024U;
	httpRequest.url = "/v1/chat/completions";
	httpRequest.body = buildChatBody(request);
	httpRequest.stream = request.options.stream;
	httpRequest.onChunk = onChunk;
	httpRequest.timeoutSeconds = control.timeoutSeconds > 0 ? control.timeoutSeconds : 180;
	httpRequest.shouldCancel = std::move(control.shouldCancel);

	const auto startedAt = std::chrono::steady_clock::now();
	const HttpResponse response = perform(httpRequest);
	result.elapsedMs = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - startedAt).count();
	result.httpStatus = response.status;
	result.rawResponse = response.body;
	result.cancelled = response.cancelled;
	result.failure = response.failure;

	if (!response.started) {
		if (result.failure == RequestFailure::None) result.failure = RequestFailure::Transport;
		result.error = response.error.empty() ? "request did not start" : response.error;
		return result;
	}
	if (response.cancelled) {
		result.failure = RequestFailure::Cancelled;
		result.text = response.streamedText;
		result.error = response.error.empty() ? "request cancelled" : response.error;
		return result;
	}
	if (response.status <= 0) {
		if (result.failure == RequestFailure::None) result.failure = RequestFailure::Transport;
		result.error = "endpoint is not reachable at " + httpRequest.url;
		if (!response.error.empty()) {
			result.error += ": " + response.error;
		}
		return result;
	}
	if (response.status < 200 || response.status >= 300) {
		result.failure = RequestFailure::Provider;
		result.error = "chat endpoint returned HTTP " + std::to_string(response.status);
		const std::string detail = extractErrorText(response.body);
		if (!detail.empty()) result.error += ": " + detail;
		else if (!response.error.empty()) result.error += ": " + response.error;
		return result;
	}
	result.text = request.options.stream
		? response.streamedText
		: extractChatText(response.body);
	if (!request.options.stream) {
		result.toolCalls = extractToolCalls(response.body);
		if (result.toolCalls.empty() && !request.tools.empty()) {
			ToolCall textCall = extractTextSerializedToolCall(result.text);
			const bool requested = std::any_of(request.tools.begin(), request.tools.end(),
				[&textCall](const ToolDefinition & tool) { return tool.name == textCall.name; });
			if (requested) {
				result.toolCalls.push_back(std::move(textCall));
				result.text.clear();
			}
		}
	}
	if (result.text.empty() && result.toolCalls.empty()) {
		result.failure = RequestFailure::InvalidResponse;
		result.error = "chat endpoint returned no text";
		return result;
	}
	result.success = true;
	if (onChunk && !request.options.stream) {
		onChunk(result.text);
	}
	return result;
}

std::string Endpoint::normalizeBaseUrl(const std::string & baseUrl) {
	std::string normalized = stripTrailingSlash(trimCopy(baseUrl));
	for (const char * suffixValue : {
		"/v1/chat/completions",
		"/chat/completions",
		"/v1/images/generations",
		"/sdcpp/v1/img_gen",
		"/sdcpp/v1/vid_gen",
		"/v1/models",
		"/models" }) {
		const std::string suffix(suffixValue);
		if (endsWith(normalized, suffix)) {
			normalized.erase(normalized.size() - suffix.size());
			break;
		}
	}
	if (endsWith(normalized, "/v1")) {
		normalized.erase(normalized.size() - 3);
	}
	return stripTrailingSlash(normalized);
}

std::string Endpoint::buildChatBody(const ChatRequest & request) {
	std::ostringstream body;
	body.imbue(std::locale::classic());
	body << "{";
	if (!request.options.model.empty()) {
		body << "\"model\":\"" << escapeJson(request.options.model) << "\",";
	}
	body << "\"messages\":[";
	bool needsComma = false;
	auto appendMessage = [&](const ChatMessage & message) {
		if (message.content.empty() && message.toolCalls.empty()) {
			return;
		}
		if (needsComma) {
			body << ",";
		}
		body << "{\"role\":\"" << roleLabel(message.role) << "\"";
		if (!message.content.empty()) {
			body << ",\"content\":\"" << escapeJson(message.content) << "\"";
		}
		if (!message.toolCallId.empty()) {
			body << ",\"tool_call_id\":\"" << escapeJson(message.toolCallId) << "\"";
		}
		if (!message.toolCalls.empty()) {
			body << ",\"tool_calls\":[";
			for (std::size_t i = 0; i < message.toolCalls.size(); ++i) {
				if (i > 0) body << ",";
				const ToolCall & call = message.toolCalls[i];
				body << "{\"id\":\"" << escapeJson(call.id)
					 << "\",\"type\":\"function\",\"function\":{\"name\":\""
					 << escapeJson(call.name) << "\",\"arguments\":\""
					 << escapeJson(call.argumentsJson) << "\"}}";
			}
			body << "]";
		}
		body << "}";
		needsComma = true;
	};
	ChatMessage systemMessage;
	systemMessage.role = ChatRole::System;
	systemMessage.content = request.systemPrompt;
	appendMessage(systemMessage);
	for (const ChatMessage & message : request.messages) {
		appendMessage(message);
	}
	body << "],";
	body << "\"max_tokens\":" << std::max(1, request.options.maxTokens) << ",";
	body << "\"temperature\":" << std::max(0.0f, request.options.temperature) << ",";
	body << "\"top_p\":" << std::clamp(request.options.topP, 0.0f, 1.0f) << ",";
	body << "\"stream\":" << (request.options.stream ? "true" : "false");
	if (request.options.seed >= 0) {
		body << ",\"seed\":" << request.options.seed;
	}
	if (!request.options.stopSequences.empty()) {
		body << ",\"stop\":[";
		for (std::size_t i = 0; i < request.options.stopSequences.size(); ++i) {
			if (i > 0) {
				body << ",";
			}
			body << "\"" << escapeJson(request.options.stopSequences[i]) << "\"";
		}
		body << "]";
	}
	if (!request.tools.empty()) {
		body << ",\"tools\":[";
		for (std::size_t i = 0; i < request.tools.size(); ++i) {
			if (i > 0) body << ",";
			const ToolDefinition & tool = request.tools[i];
			body << "{\"type\":\"function\",\"function\":{\"name\":\""
				 << escapeJson(tool.name) << "\",\"description\":\""
				 << escapeJson(tool.description) << "\",\"parameters\":"
				 << (tool.parametersJson.empty() ? "{}" : tool.parametersJson) << "}}";
		}
		body << "],\"tool_choice\":\"auto\"";
	}
	body << "}";
	return body.str();
}

std::string Endpoint::extractChatText(const std::string & responseBody) {
	return extractChatTextValue(responseBody);
}

std::string Endpoint::extractErrorText(const std::string & responseBody) {
	const auto bounded = [](std::string value) {
		for (char & character : value) {
			const unsigned char byte = static_cast<unsigned char>(character);
			if (byte < 0x20U || byte == 0x7fU) character = ' ';
		}
		value = trimCopy(value);
		return value.size() <= 512 ? value : value.substr(0, 512) + "...";
	};
	for (const char * key : { "message", "error", "detail" }) {
		const std::string value = bounded(extractJsonStringField(responseBody, key));
		if (!value.empty()) return value;
	}

	std::string value = trimCopy(responseBody);
	if (value.empty() || value.front() == '{' || value.front() == '[' || value.front() == '<') {
		return {};
	}
	return bounded(std::move(value));
}

std::vector<ToolCall> Endpoint::extractToolCalls(const std::string & responseBody) {
	std::vector<ToolCall> calls;
	const std::size_t keyPosition = responseBody.find("\"tool_calls\"");
	if (keyPosition == std::string::npos) return calls;
	const std::size_t arrayStart = responseBody.find('[', keyPosition);
	if (arrayStart == std::string::npos) return calls;
	const std::size_t arrayEnd = findMatchingJsonDelimiter(responseBody, arrayStart, '[', ']');
	if (arrayEnd == std::string::npos) return calls;

	std::size_t searchFrom = arrayStart + 1;
	while (searchFrom < arrayEnd) {
		const std::size_t objectStart = responseBody.find('{', searchFrom);
		if (objectStart == std::string::npos || objectStart >= arrayEnd) break;
		const std::size_t objectEnd = findMatchingJsonDelimiter(responseBody, objectStart, '{', '}');
		if (objectEnd == std::string::npos || objectEnd > arrayEnd) break;
		const std::size_t functionKey = responseBody.find("\"function\"", objectStart);
		if (functionKey == std::string::npos || functionKey >= objectEnd) break;
		const std::size_t functionStart = responseBody.find('{', functionKey);
		const std::size_t functionEnd = findMatchingJsonDelimiter(responseBody, functionStart, '{', '}');
		if (functionStart == std::string::npos || functionEnd == std::string::npos || functionEnd > objectEnd) break;

		ToolCall call;
		call.id = extractScopedJsonStringField(responseBody, "id", objectStart, objectEnd);
		call.name = extractScopedJsonStringField(responseBody, "name", functionStart, functionEnd);
		call.argumentsJson = extractScopedJsonStringField(
			responseBody, "arguments", functionStart, functionEnd);
		if (!call.id.empty() && !call.name.empty()) {
			calls.push_back(std::move(call));
		}
		searchFrom = objectEnd + 1;
	}
	return calls;
}

std::vector<std::string> Endpoint::extractModelIds(const std::string & responseBody) {
	std::vector<std::string> models;
	const std::string key = "\"id\"";
	std::size_t searchFrom = 0;
	while (true) {
		const std::size_t keyPosition = responseBody.find(key, searchFrom);
		if (keyPosition == std::string::npos) {
			break;
		}
		const std::size_t colon = responseBody.find(':', keyPosition + key.size());
		if (colon == std::string::npos) {
			break;
		}
		const std::string id = extractJsonStringAt(responseBody, colon + 1);
		if (!id.empty()) {
			models.push_back(id);
		}
		searchFrom = colon + 1;
	}
	return models;
}

HttpResponse Endpoint::runHttpRequest(const HttpRequest & request) {
	HttpResponse result;
	if (request.url.empty()) {
		result.error = "request URL is empty";
		return result;
	}
#if defined(_WIN32) && !defined(OFXIC_HAS_OF_HTTP_RUNTIME)
	if (!request.stream) return runWinHttpRequest(request);
	result.error = "streaming requests require the openFrameworks curl runtime";
	return result;
#endif
#if defined(OFXIC_HAS_OF_HTTP_RUNTIME)
#if defined(_WIN32)
	if (!request.stream && request.url.compare(0, 8, "https://") == 0) {
		return runWinHttpRequest(request);
	}
#endif
#if defined(OFXIC_HAS_CURL_HTTP_RUNTIME)
	if (request.stream || request.shouldCancel) {
		return runCurlRequest(request);
	}
#else
	if (request.stream) {
		result.error = "streaming requests require curl";
		return result;
	}
#endif
	ofHttpRequest ofRequest(request.url, "ofxIC-endpoint");
	ofRequest.method = request.method == HttpMethod::Post
		? ofHttpRequest::POST
		: ofHttpRequest::GET;
	ofRequest.body = request.body;
	ofRequest.contentType = request.contentType;
	ofRequest.headers["Accept"] = request.accept;
	ofRequest.headers["Content-Type"] = request.contentType;
	for (const auto & header : request.headers) {
		ofRequest.headers[header.first] = header.second;
	}
	ofRequest.timeoutSeconds = request.timeoutSeconds;

	ofURLFileLoader loader;
	const ofHttpResponse response = loader.handleRequest(ofRequest);
	result.started = true;
	result.status = response.status;
	result.body = response.data.getText();
	result.error = response.error;
	if (result.body.size() > request.maxResponseBytes) {
		result.status = 0;
		result.body.clear();
		result.error = "response exceeded " +
			std::to_string(request.maxResponseBytes) + " byte limit";
	}
	return result;
#else
	result.error = "HTTP requests require the openFrameworks runtime";
	return result;
#endif
}

} // namespace ofxIC
