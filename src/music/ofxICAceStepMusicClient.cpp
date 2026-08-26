#include "ofxICAceStepMusicClient.h"

#include "../endpoint/ofxICEndpoint.h"

#include <algorithm>
#include <cctype>
#include <locale>
#include <sstream>
#include <utility>

namespace ofxIC {
namespace {

constexpr std::size_t maxJsonResponseBytes = 4U * 1024U * 1024U;
constexpr std::size_t maxAudioResponseBytes = 128U * 1024U * 1024U;

AceStepMusicJob failure(
	std::string message,
	int httpStatus = 0,
	std::string rawResponse = {},
	RequestFailure requestFailure = RequestFailure::InvalidResponse,
	bool cancelled = false) {
	AceStepMusicJob result;
	result.cancelled = cancelled;
	result.failure = requestFailure;
	result.httpStatus = httpStatus;
	result.state = AceStepMusicJobState::Failed;
	result.error = std::move(message);
	result.rawResponse = std::move(rawResponse);
	return result;
}

RequestFailure responseFailureKind(const HttpResponse & response) {
	if (response.cancelled) return RequestFailure::Cancelled;
	if (response.failure != RequestFailure::None) return response.failure;
	return response.status > 0 ? RequestFailure::Provider : RequestFailure::Transport;
}

std::string trimCopy(const std::string & value) {
	std::size_t first = 0;
	while (first < value.size() &&
		std::isspace(static_cast<unsigned char>(value[first]))) ++first;
	std::size_t last = value.size();
	while (last > first &&
		std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
	return value.substr(first, last - first);
}

std::string escapeJson(const std::string & value) {
	std::ostringstream escaped;
	for (const unsigned char character : value) {
		switch (character) {
		case '\\': escaped << "\\\\"; break;
		case '"': escaped << "\\\""; break;
		case '\b': escaped << "\\b"; break;
		case '\f': escaped << "\\f"; break;
		case '\n': escaped << "\\n"; break;
		case '\r': escaped << "\\r"; break;
		case '\t': escaped << "\\t"; break;
		default:
			if (character < 0x20U) {
				const char * hex = "0123456789abcdef";
				escaped << "\\u00" << hex[(character >> 4U) & 0x0fU]
					<< hex[character & 0x0fU];
			} else {
				escaped << static_cast<char>(character);
			}
		}
	}
	return escaped.str();
}

std::string number(float value) {
	std::ostringstream stream;
	stream.imbue(std::locale::classic());
	stream << value;
	return stream.str();
}

std::string requestBody(const AceStepMusicRequest & request) {
	const std::string lyrics = request.instrumentalOnly ? "[Instrumental]" : request.lyrics;
	return "{\"caption\":\"" + escapeJson(request.caption) +
		"\",\"lyrics\":\"" + escapeJson(lyrics) +
		"\",\"bpm\":" + std::to_string(std::max(0, request.bpm)) +
		",\"duration\":" + std::to_string(request.durationSeconds) +
		",\"keyscale\":\"" + escapeJson(request.keyScale) +
		"\",\"timesignature\":\"" + escapeJson(request.timeSignature) +
		"\",\"seed\":" + std::to_string(request.seed) +
		",\"batch_size\":1" +
		",\"lm_temperature\":" + number(0.85f) +
		",\"lm_cfg_scale\":" + number(2.0f) +
		",\"lm_top_p\":" + number(0.9f) +
		",\"lm_top_k\":0" +
		",\"lm_negative_prompt\":\"" + escapeJson(request.negativePrompt) +
		"\",\"use_cot_caption\":true" +
		",\"audio_codes\":\"\"" +
		",\"inference_steps\":0" +
		",\"guidance_scale\":0" +
		",\"shift\":0" +
		",\"audio_cover_strength\":0.5" +
		",\"repainting_start\":-1" +
		",\"repainting_end\":-1" +
		",\"lego\":\"\"" +
		",\"output_format\":\"" +
		(request.outputFormat == "wav" ? "wav16" : "mp3") + "\"}";
}

std::string validate(const AceStepMusicRequest & request) {
	if (trimCopy(request.caption).empty()) return "music caption is empty";
	if (request.caption.size() > 10000U) return "music caption exceeds the 10000-byte limit";
	if (request.lyrics.size() > 100000U) return "music lyrics exceed the 100000-byte limit";
	if (request.durationSeconds < 1 || request.durationSeconds > 600) {
		return "duration must be between 1 and 600 seconds";
	}
	if (request.bpm < 0 || request.bpm > 400) return "bpm must be between 0 and 400";
	if (request.timeSignature.size() > 16U || request.keyScale.size() > 64U) {
		return "music key or time signature is too long";
	}
	if (request.outputFormat != "mp3" && request.outputFormat != "wav") {
		return "output format must be mp3 or wav";
	}
	return {};
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

std::string extractStringField(const std::string & json, const std::string & key) {
	const std::string quotedKey = "\"" + key + "\"";
	const std::size_t keyPosition = json.find(quotedKey);
	if (keyPosition == std::string::npos) return {};
	std::size_t position = json.find(':', keyPosition + quotedKey.size());
	if (position == std::string::npos) return {};
	++position;
	while (position < json.size() &&
		std::isspace(static_cast<unsigned char>(json[position]))) ++position;
	if (position >= json.size() || json[position] != '"') return {};
	++position;
	std::string result;
	bool escaped = false;
	for (; position < json.size(); ++position) {
		const char character = json[position];
		if (escaped) {
			switch (character) {
			case 'n': result.push_back('\n'); break;
			case 'r': result.push_back('\r'); break;
			case 't': result.push_back('\t'); break;
			default: result.push_back(character); break;
			}
			escaped = false;
		} else if (character == '\\') {
			escaped = true;
		} else if (character == '"') {
			return result;
		} else {
			result.push_back(character);
		}
	}
	return {};
}

std::size_t matchingDelimiter(
	const std::string & json,
	std::size_t openPosition,
	char openDelimiter,
	char closeDelimiter) {
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

std::string likelyJson(const std::string & responseBody) {
	const std::string trimmed = trimCopy(responseBody);
	const std::size_t object = trimmed.find('{');
	const std::size_t array = trimmed.find('[');
	std::size_t first = std::string::npos;
	if (object == std::string::npos) first = array;
	else if (array == std::string::npos) first = object;
	else first = std::min(object, array);
	if (first == std::string::npos) return {};
	const char open = trimmed[first];
	const char close = open == '{' ? '}' : ']';
	const std::size_t last = matchingDelimiter(trimmed, first, open, close);
	return last == std::string::npos ? std::string{} : trimmed.substr(first, last - first + 1U);
}

std::string nestedJsonValue(const std::string & json, const char * key) {
	const std::string quotedKey = "\"" + std::string(key) + "\"";
	const std::size_t keyPosition = json.find(quotedKey);
	if (keyPosition == std::string::npos) return {};
	std::size_t position = json.find(':', keyPosition + quotedKey.size());
	if (position == std::string::npos) return {};
	++position;
	while (position < json.size() &&
		std::isspace(static_cast<unsigned char>(json[position]))) ++position;
	if (position >= json.size() || (json[position] != '{' && json[position] != '[')) return {};
	const char open = json[position];
	const char close = open == '{' ? '}' : ']';
	const std::size_t end = matchingDelimiter(json, position, open, close);
	return end == std::string::npos ? std::string{} : json.substr(position, end - position + 1U);
}

std::string synthRequestBody(const std::string & languageModelResponse, const std::string & format) {
	std::string body = likelyJson(languageModelResponse);
	if (body.empty()) return {};
	if (body.front() == '{') {
		for (const char * key : { "result", "request", "requests", "data", "payload" }) {
			const std::string nested = nestedJsonValue(body, key);
			if (!nested.empty()) {
				body = nested;
				break;
			}
		}
	}
	if (body.front() == '{' && body.back() == '}') {
		const bool empty = trimCopy(body.substr(1U, body.size() - 2U)).empty();
		body.insert(body.size() - 1U,
			std::string(empty ? "" : ",") + "\"output_format\":\"" +
			(format == "wav" ? "wav16" : "mp3") + "\"");
	}
	return body;
}

bool validJobId(const std::string & id) {
	return !id.empty() && id.size() <= 128U &&
		std::all_of(id.begin(), id.end(), [](unsigned char character) {
			return std::isalnum(character) != 0 || character == '-' || character == '_';
		});
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

std::string lowerCopy(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

std::size_t boundaryLine(
	const std::string & body,
	const std::string & delimiter,
	std::size_t cursor) {
	while (cursor < body.size()) {
		const std::size_t boundary = body.find(delimiter, cursor);
		if (boundary == std::string::npos || boundary == 0 || body[boundary - 1] == '\n') {
			return boundary;
		}
		cursor = boundary + delimiter.size();
	}
	return std::string::npos;
}

std::string extractAudioPayload(const std::string & responseBody) {
	if (responseBody.compare(0, 2, "--") != 0) return responseBody;
	std::size_t firstLineEnd = responseBody.find("\r\n");
	std::size_t lineBreakSize = 2;
	if (firstLineEnd == std::string::npos) {
		firstLineEnd = responseBody.find('\n');
		lineBreakSize = 1;
	}
	if (firstLineEnd == std::string::npos) return {};
	const std::string delimiter = responseBody.substr(0, firstLineEnd);
	if (delimiter.size() <= 2U) return {};

	std::size_t cursor = 0;
	while (cursor < responseBody.size()) {
		std::size_t partBegin = boundaryLine(responseBody, delimiter, cursor);
		if (partBegin == std::string::npos) break;
		partBegin += delimiter.size();
		if (partBegin + 1U < responseBody.size() &&
			responseBody.compare(partBegin, 2, "--") == 0) break;
		if (partBegin + lineBreakSize <= responseBody.size() &&
			responseBody.compare(partBegin, lineBreakSize,
				lineBreakSize == 2 ? "\r\n" : "\n") == 0) {
			partBegin += lineBreakSize;
		}
		const std::size_t nextPart = boundaryLine(responseBody, delimiter, partBegin);
		if (nextPart == std::string::npos) break;
		std::size_t partEnd = nextPart;
		if (partEnd >= 2U && responseBody.compare(partEnd - 2U, 2, "\r\n") == 0) partEnd -= 2U;
		else if (partEnd >= 1U && responseBody[partEnd - 1U] == '\n') --partEnd;
		const std::string part = partEnd > partBegin
			? responseBody.substr(partBegin, partEnd - partBegin)
			: std::string{};
		std::size_t headerEnd = part.find("\r\n\r\n");
		std::size_t bodyBegin = headerEnd == std::string::npos
			? std::string::npos
			: headerEnd + 4U;
		if (bodyBegin == std::string::npos) {
			headerEnd = part.find("\n\n");
			if (headerEnd != std::string::npos) bodyBegin = headerEnd + 2U;
		}
		if (bodyBegin != std::string::npos &&
			lowerCopy(part.substr(0, headerEnd)).find("content-type: audio/") != std::string::npos) {
			return part.substr(bodyBegin);
		}
		cursor = nextPart;
	}
	return {};
}

AceStepMusicJob submittedJob(
	const std::string & id,
	const std::string & format,
	AceStepMusicJobPhase phase,
	int httpStatus,
	const std::string & rawResponse) {
	AceStepMusicJob result;
	result.success = true;
	result.httpStatus = httpStatus;
	result.state = AceStepMusicJobState::Submitted;
	result.phase = phase;
	result.id = id;
	result.outputFormat = format;
	result.mimeType = format == "wav" ? "audio/wav" : "audio/mpeg";
	result.rawResponse = rawResponse;
	return result;
}

AceStepMusicJob completedJob(
	const std::string & bytes,
	const std::string & format,
	int httpStatus) {
	const std::string audioBytes = extractAudioPayload(bytes);
	if (!hasExpectedAudioSignature(audioBytes, format)) {
		return failure("ACE-Step synthesis returned success without valid " + format + " audio", httpStatus);
	}
	AceStepMusicJob result;
	result.success = true;
	result.httpStatus = httpStatus;
	result.state = AceStepMusicJobState::Completed;
	result.phase = AceStepMusicJobPhase::Synthesis;
	result.outputFormat = format;
	result.mimeType = format == "wav" ? "audio/wav" : "audio/mpeg";
	result.audioBytes = audioBytes;
	return result;
}

} // namespace

AceStepMusicClient::AceStepMusicClient(Endpoint & endpoint)
	: endpoint(endpoint) {}

AceStepMusicJob AceStepMusicClient::submit(
	const AceStepMusicRequest & request, RequestControl control) const {
	if (control.timeoutSeconds < 0) return failure("request timeout cannot be negative");
	const std::string validationError = validate(request);
	if (!validationError.empty()) return failure(validationError);

	HttpRequest httpRequest;
	httpRequest.method = HttpMethod::Post;
	httpRequest.url = "/lm";
	httpRequest.body = requestBody(request);
	httpRequest.contentType = "application/json";
	httpRequest.accept = "application/json";
	httpRequest.timeoutSeconds = control.timeoutSeconds > 0 ? control.timeoutSeconds : 240;
	httpRequest.shouldCancel = control.shouldCancel;
	httpRequest.maxResponseBytes = maxJsonResponseBytes;
	httpRequest.useBearerToken = false;
	const HttpResponse response = endpoint.perform(std::move(httpRequest));
	if (response.status < 200 || response.status >= 300) {
		return failure(responseFailure(
			response, "ACE-Step /lm", Endpoint::extractErrorText(response.body)),
			response.status, response.body, responseFailureKind(response), response.cancelled);
	}
	const std::string id = extractStringField(response.body, "id");
	if (!id.empty()) {
		if (!validJobId(id)) return failure("ACE-Step /lm returned an invalid job id", response.status, response.body);
		return submittedJob(id, request.outputFormat, AceStepMusicJobPhase::LanguageModel,
			response.status, response.body);
	}
	return submitSynthesis(response.body, request.outputFormat, std::move(control));
}

AceStepMusicJob AceStepMusicClient::submitSynthesis(
	const std::string & languageModelResponse,
	const std::string & outputFormat,
	RequestControl control) const {
	const std::string body = synthRequestBody(languageModelResponse, outputFormat);
	if (body.empty()) return failure("ACE-Step /lm returned no usable synthesis request JSON");

	HttpRequest request;
	request.method = HttpMethod::Post;
	request.url = "/synth";
	request.body = body;
	request.contentType = "application/json";
	request.accept = outputFormat == "wav" ? "audio/wav" : "audio/mpeg";
	request.timeoutSeconds = control.timeoutSeconds > 0 ? control.timeoutSeconds : 900;
	request.shouldCancel = std::move(control.shouldCancel);
	request.maxResponseBytes = maxAudioResponseBytes;
	request.useBearerToken = false;
	const HttpResponse response = endpoint.perform(std::move(request));
	if (response.status < 200 || response.status >= 300) {
		return failure(responseFailure(
			response, "ACE-Step /synth", Endpoint::extractErrorText(response.body)),
			response.status, response.body, responseFailureKind(response), response.cancelled);
	}
	const std::string id = extractStringField(response.body, "id");
	if (!id.empty()) {
		if (!validJobId(id)) return failure("ACE-Step /synth returned an invalid job id", response.status, response.body);
		return submittedJob(id, outputFormat, AceStepMusicJobPhase::Synthesis,
			response.status, response.body);
	}
	return completedJob(response.body, outputFormat, response.status);
}

AceStepMusicJob AceStepMusicClient::poll(
	const AceStepMusicJob & job, RequestControl control) const {
	if (control.timeoutSeconds < 0) return failure("request timeout cannot be negative");
	if (!validJobId(job.id)) return failure("ACE-Step music job id is invalid");
	if (job.outputFormat != "mp3" && job.outputFormat != "wav") {
		return failure("ACE-Step music job output format must be mp3 or wav");
	}

	HttpRequest statusRequest;
	statusRequest.method = HttpMethod::Get;
	statusRequest.url = "/job?id=" + job.id;
	statusRequest.accept = "application/json";
	statusRequest.timeoutSeconds = control.timeoutSeconds > 0 ? control.timeoutSeconds : 30;
	statusRequest.shouldCancel = control.shouldCancel;
	statusRequest.maxResponseBytes = maxJsonResponseBytes;
	statusRequest.useBearerToken = false;
	const HttpResponse statusResponse = endpoint.perform(std::move(statusRequest));
	if (statusResponse.status < 200 || statusResponse.status >= 300) {
		AceStepMusicJob result = failure(
			responseFailure(statusResponse, "ACE-Step /job",
				Endpoint::extractErrorText(statusResponse.body)),
			statusResponse.status, statusResponse.body,
			responseFailureKind(statusResponse), statusResponse.cancelled);
		result.id = job.id;
		result.outputFormat = job.outputFormat;
		result.phase = job.phase;
		return result;
	}
	const std::string status = extractStringField(statusResponse.body, "status");
	const std::string statusError = extractStringField(statusResponse.body, "error");
	if (status == "failed" || status == "cancelled") {
		AceStepMusicJob result = failure(
			"ACE-Step job " + status + (statusError.empty() ? std::string{} : ": " + statusError),
			statusResponse.status, statusResponse.body);
		result.id = job.id;
		result.outputFormat = job.outputFormat;
		result.phase = job.phase;
		return result;
	}
	if (status != "done") {
		if (status.empty()) {
			return failure(statusError.empty()
				? "ACE-Step /job returned invalid status JSON"
				: "ACE-Step /job failed: " + statusError,
				statusResponse.status, statusResponse.body);
		}
		AceStepMusicJob result = job;
		result.success = true;
		result.httpStatus = statusResponse.status;
		result.state = AceStepMusicJobState::Generating;
		result.error.clear();
		result.rawResponse = statusResponse.body;
		return result;
	}

	HttpRequest resultRequest;
	resultRequest.method = HttpMethod::Get;
	resultRequest.url = "/job?id=" + job.id + "&result=1";
	resultRequest.accept = job.phase == AceStepMusicJobPhase::LanguageModel
		? "application/json"
		: (job.outputFormat == "wav" ? "audio/wav" : "audio/mpeg");
	resultRequest.timeoutSeconds = control.timeoutSeconds > 0 ? control.timeoutSeconds :
		(job.phase == AceStepMusicJobPhase::LanguageModel ? 60 : 900);
	resultRequest.shouldCancel = control.shouldCancel;
	resultRequest.maxResponseBytes = job.phase == AceStepMusicJobPhase::LanguageModel
		? maxJsonResponseBytes
		: maxAudioResponseBytes;
	resultRequest.useBearerToken = false;
	const HttpResponse response = endpoint.perform(std::move(resultRequest));
	if (response.status < 200 || response.status >= 300) {
		return failure(responseFailure(response, "ACE-Step /job result",
			Endpoint::extractErrorText(response.body)),
			response.status, response.body, responseFailureKind(response), response.cancelled);
	}
	if (job.phase == AceStepMusicJobPhase::LanguageModel) {
		return submitSynthesis(response.body, job.outputFormat, std::move(control));
	}
	AceStepMusicJob result = completedJob(response.body, job.outputFormat, response.status);
	result.id = job.id;
	return result;
}

} // namespace ofxIC
