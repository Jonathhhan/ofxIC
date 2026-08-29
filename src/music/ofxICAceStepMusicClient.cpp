#include "ofxICAceStepMusicClient.h"

#include "../endpoint/ofxICEndpoint.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace ofxIC {
namespace {

constexpr std::size_t maxJsonResponseBytes = 4U * 1024U * 1024U;
constexpr std::size_t maxAudioResponseBytes = 256U * 1024U * 1024U;

AceStepMusicJob failure(std::string message, int httpStatus = 0,
	std::string rawResponse = {},
	RequestFailure requestFailure = RequestFailure::InvalidResponse,
	bool cancelled = false) {
	AceStepMusicJob result;
	result.cancelled = cancelled;
	result.failure = requestFailure;
	result.httpStatus = httpStatus;
	result.state = AceStepMusicJobState::Failed;
	result.phase = AceStepMusicJobPhase::Synthesis;
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
	while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
	std::size_t last = value.size();
	while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
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
			} else escaped << static_cast<char>(character);
		}
	}
	return escaped.str();
}

std::size_t valuePosition(const std::string & json, const std::string & key,
	std::size_t from = 0) {
	const std::string quotedKey = "\"" + key + "\"";
	const std::size_t keyPosition = json.find(quotedKey, from);
	if (keyPosition == std::string::npos) return std::string::npos;
	std::size_t position = json.find(':', keyPosition + quotedKey.size());
	if (position == std::string::npos) return std::string::npos;
	++position;
	while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) ++position;
	return position;
}

std::string extractStringField(const std::string & json, const std::string & key,
	std::size_t from = 0) {
	std::size_t position = valuePosition(json, key, from);
	if (position == std::string::npos || position >= json.size() || json[position] != '"') return {};
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
			case 'b': result.push_back('\b'); break;
			case 'f': result.push_back('\f'); break;
			default: result.push_back(character); break;
			}
			escaped = false;
		} else if (character == '\\') escaped = true;
		else if (character == '"') return result;
		else result.push_back(character);
	}
	return {};
}

bool extractIntegerField(const std::string & json, const std::string & key,
	int & value, std::size_t from = 0) {
	std::size_t position = valuePosition(json, key, from);
	if (position == std::string::npos || position >= json.size()) return false;
	bool negative = false;
	if (json[position] == '-') { negative = true; ++position; }
	if (position >= json.size() || !std::isdigit(static_cast<unsigned char>(json[position]))) return false;
	long long parsed = 0;
	while (position < json.size() && std::isdigit(static_cast<unsigned char>(json[position]))) {
		parsed = parsed * 10 + (json[position++] - '0');
		if (parsed > 2147483647LL) return false;
	}
	value = static_cast<int>(negative ? -parsed : parsed);
	return true;
}

std::string requestBody(const AceStepMusicRequest & request) {
	const std::string lyrics = request.instrumentalOnly ? "" : request.lyrics;
	std::string body = "{\"prompt\":\"" + escapeJson(request.caption) +
		"\",\"lyrics\":\"" + escapeJson(lyrics) +
		"\",\"task_type\":\"text2music\"" +
		",\"audio_duration\":" + std::to_string(request.durationSeconds) +
		",\"batch_size\":1" +
		",\"model\":\"" + escapeJson(request.model) + "\"" +
		",\"audio_format\":\"" + escapeJson(request.outputFormat) + "\"" +
		",\"thinking\":" + (request.thinking ? "true" : "false") +
		",\"use_format\":" + (request.useFormat ? "true" : "false") +
		",\"vocal_language\":\"" + escapeJson(request.vocalLanguage) + "\"" +
		",\"inference_steps\":" + std::to_string(request.inferenceSteps);
	if (request.seed >= 0) body += ",\"seed\":" + std::to_string(request.seed) + ",\"use_random_seed\":false";
	else body += ",\"use_random_seed\":true";
	if (request.bpm > 0) body += ",\"bpm\":" + std::to_string(request.bpm);
	if (!request.keyScale.empty()) body += ",\"key_scale\":\"" + escapeJson(request.keyScale) + "\"";
	if (!request.timeSignature.empty()) body += ",\"time_signature\":\"" + escapeJson(request.timeSignature) + "\"";
	if (!request.negativePrompt.empty()) body += ",\"negative_prompt\":\"" + escapeJson(request.negativePrompt) + "\"";
	return body + "}";
}

std::string validate(const AceStepMusicRequest & request) {
	if (trimCopy(request.caption).empty()) return "music caption is empty";
	if (request.caption.size() > 10000U) return "music caption exceeds the 10000-byte limit";
	if (request.lyrics.size() > 100000U) return "music lyrics exceeds the 100000-byte limit";
	if (request.durationSeconds < 10 || request.durationSeconds > 600) return "duration must be between 10 and 600 seconds";
	if (request.bpm < 0 || request.bpm > 400) return "bpm must be between 0 and 400";
	if (request.inferenceSteps < 1 || request.inferenceSteps > 200) return "inference steps must be between 1 and 200";
	if (request.model.empty() || request.model.size() > 256U) return "ACE-Step model id is invalid";
	if (request.vocalLanguage.empty() || request.vocalLanguage.size() > 32U) return "vocal language is invalid";
	if (request.timeSignature.size() > 16U || request.keyScale.size() > 64U) return "music key or time signature is too long";
	if (request.outputFormat != "mp3" && request.outputFormat != "wav") return "output format must be mp3 or wav";
	return {};
}

std::string responseFailure(const HttpResponse & response, const char * operation) {
	if (!response.error.empty()) return response.error;
	std::string detail = extractStringField(response.body, "detail");
	if (detail.empty()) detail = extractStringField(response.body, "error");
	if (detail.empty()) detail = extractStringField(response.body, "message");
	if (response.status > 0) return std::string(operation) + " returned HTTP " +
		std::to_string(response.status) + (detail.empty() ? std::string{} : ": " + detail);
	return std::string(operation) + " failed before an HTTP response was received";
}

bool validJobId(const std::string & id) {
	return !id.empty() && id.size() <= 128U && std::all_of(id.begin(), id.end(),
		[](unsigned char character) { return std::isalnum(character) != 0 || character == '-' || character == '_'; });
}

bool validAudioPath(const std::string & value) {
	return value.rfind("/v1/audio?path=", 0) == 0 && value.size() <= 4096U &&
		value.find_first_of("\r\n\\") == std::string::npos;
}

bool hasExpectedAudioSignature(const std::string & bytes, const std::string & format) {
	if (format == "wav") return bytes.size() >= 12U && bytes.compare(0, 4, "RIFF") == 0 && bytes.compare(8, 4, "WAVE") == 0;
	if (bytes.size() >= 3U && bytes.compare(0, 3, "ID3") == 0) return true;
	return bytes.size() >= 2U && static_cast<unsigned char>(bytes[0]) == 0xffU &&
		(static_cast<unsigned char>(bytes[1]) & 0xe0U) == 0xe0U;
}

AceStepMusicJob submittedJob(const std::string & id, const std::string & format,
	int status, const std::string & rawResponse) {
	AceStepMusicJob result;
	result.success = true;
	result.httpStatus = status;
	result.state = AceStepMusicJobState::Submitted;
	result.phase = AceStepMusicJobPhase::Synthesis;
	result.id = id;
	result.outputFormat = format;
	result.mimeType = format == "wav" ? "audio/wav" : "audio/mpeg";
	result.rawResponse = rawResponse;
	return result;
}

} // namespace

AceStepMusicClient::AceStepMusicClient(Endpoint & endpoint) : endpoint(endpoint) {}

AceStepMusicJob AceStepMusicClient::submit(const AceStepMusicRequest & request,
	RequestControl control) const {
	if (control.timeoutSeconds < 0) return failure("request timeout cannot be negative");
	const std::string validationError = validate(request);
	if (!validationError.empty()) return failure(validationError);
	HttpRequest httpRequest;
	httpRequest.method = HttpMethod::Post;
	httpRequest.url = "/release_task";
	httpRequest.body = requestBody(request);
	httpRequest.contentType = "application/json";
	httpRequest.accept = "application/json";
	httpRequest.timeoutSeconds = control.timeoutSeconds > 0 ? control.timeoutSeconds : 240;
	httpRequest.shouldCancel = std::move(control.shouldCancel);
	httpRequest.maxResponseBytes = maxJsonResponseBytes;
	httpRequest.useBearerToken = false;
	const HttpResponse response = endpoint.perform(std::move(httpRequest));
	if (response.status < 200 || response.status >= 300)
		return failure(responseFailure(response, "ACE-Step /release_task"), response.status,
			response.body, responseFailureKind(response), response.cancelled);
	int apiCode = 0;
	if (!extractIntegerField(response.body, "code", apiCode) || apiCode != 200) {
		const std::string apiError = extractStringField(response.body, "error");
		return failure(apiError.empty() ? "ACE-Step returned an invalid release_task response"
			: "ACE-Step rejected the task: " + apiError, response.status, response.body);
	}
	const std::string id = extractStringField(response.body, "task_id");
	if (!validJobId(id)) return failure("ACE-Step /release_task returned an invalid task id", response.status, response.body);
	return submittedJob(id, request.outputFormat, response.status, response.body);
}

AceStepMusicJob AceStepMusicClient::poll(const AceStepMusicJob & job,
	RequestControl control) const {
	if (control.timeoutSeconds < 0) return failure("request timeout cannot be negative");
	if (!validJobId(job.id)) return failure("ACE-Step music job id is invalid");
	if (job.outputFormat != "mp3" && job.outputFormat != "wav") return failure("ACE-Step music job output format must be mp3 or wav");
	HttpRequest query;
	query.method = HttpMethod::Post;
	query.url = "/query_result";
	query.body = "{\"task_id_list\":[\"" + escapeJson(job.id) + "\"]}";
	query.contentType = "application/json";
	query.accept = "application/json";
	query.timeoutSeconds = control.timeoutSeconds > 0 ? control.timeoutSeconds : 30;
	query.shouldCancel = control.shouldCancel;
	query.maxResponseBytes = maxJsonResponseBytes;
	query.useBearerToken = false;
	const HttpResponse response = endpoint.perform(std::move(query));
	if (response.status < 200 || response.status >= 300) {
		AceStepMusicJob result = failure(responseFailure(response, "ACE-Step /query_result"), response.status,
			response.body, responseFailureKind(response), response.cancelled);
		result.id = job.id;
		result.outputFormat = job.outputFormat;
		return result;
	}
	int apiCode = 0;
	int taskStatus = -1;
	const std::size_t taskPosition = response.body.find("\"task_id\"");
	if (!extractIntegerField(response.body, "code", apiCode) || apiCode != 200 ||
		taskPosition == std::string::npos || extractStringField(response.body, "task_id", taskPosition) != job.id ||
		!extractIntegerField(response.body, "status", taskStatus, taskPosition))
		return failure("ACE-Step /query_result returned an invalid response", response.status, response.body);
	if (taskStatus == 0) {
		AceStepMusicJob result = job;
		result.success = true;
		result.httpStatus = response.status;
		result.state = AceStepMusicJobState::Generating;
		result.phase = AceStepMusicJobPhase::Synthesis;
		result.error.clear();
		result.rawResponse = response.body;
		return result;
	}
	if (taskStatus == 2) {
		std::string detail = extractStringField(response.body, "error", taskPosition);
		if (detail.empty()) detail = extractStringField(response.body, "result", taskPosition);
		AceStepMusicJob result = failure("ACE-Step generation failed" + (detail.empty() ? std::string{} : ": " + detail),
			response.status, response.body);
		result.id = job.id;
		result.outputFormat = job.outputFormat;
		return result;
	}
	if (taskStatus != 1) return failure("ACE-Step returned unsupported task status " + std::to_string(taskStatus), response.status, response.body);
	const std::string encodedResult = extractStringField(response.body, "result", taskPosition);
	const std::string audioPath = extractStringField(encodedResult, "file");
	if (!validAudioPath(audioPath)) return failure("ACE-Step completed without a safe /v1/audio result URL", response.status, response.body);
	HttpRequest download;
	download.method = HttpMethod::Get;
	download.url = audioPath;
	download.accept = job.outputFormat == "wav" ? "audio/wav" : "audio/mpeg";
	download.timeoutSeconds = control.timeoutSeconds > 0 ? control.timeoutSeconds : 300;
	download.shouldCancel = std::move(control.shouldCancel);
	download.maxResponseBytes = maxAudioResponseBytes;
	download.useBearerToken = false;
	const HttpResponse audio = endpoint.perform(std::move(download));
	if (audio.status < 200 || audio.status >= 300)
		return failure(responseFailure(audio, "ACE-Step audio download"), audio.status,
			audio.body, responseFailureKind(audio), audio.cancelled);
	if (!hasExpectedAudioSignature(audio.body, job.outputFormat)) return failure("ACE-Step audio download returned invalid " + job.outputFormat + " data", audio.status);
	AceStepMusicJob result = job;
	result.success = true;
	result.httpStatus = audio.status;
	result.state = AceStepMusicJobState::Completed;
	result.phase = AceStepMusicJobPhase::Synthesis;
	result.mimeType = job.outputFormat == "wav" ? "audio/wav" : "audio/mpeg";
	result.audioBytes = audio.body;
	result.error.clear();
	result.rawResponse = response.body;
	return result;
}

} // namespace ofxIC
