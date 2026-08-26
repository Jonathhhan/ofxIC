#include "test_harness.h"
#include "../src/ofxIC.h"

#include <string>
#include <utility>
#include <vector>

namespace {

std::string wavBytes() {
	return std::string("RIFF", 4) + std::string(4, '\0') + "WAVEfmt ";
}

OFXIC_TEST(acestep_music_forwards_control_and_classifies_cancellation) {
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("http://localhost:8001", [&](const ofxIC::HttpRequest & request) {
		captured = request;
		ofxIC::HttpResponse response;
		response.cancelled = true;
		response.failure = ofxIC::RequestFailure::Cancelled;
		response.error = "request cancelled";
		return response;
	});
	ofxIC::AceStepMusicClient music(endpoint);
	ofxIC::AceStepMusicRequest request;
	request.caption = "cancelled music";
	ofxIC::RequestControl control;
	control.timeoutSeconds = 11;
	control.shouldCancel = [] { return true; };

	const auto result = music.submit(request, control);
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.cancelled);
	OFXIC_REQUIRE(result.failure == ofxIC::RequestFailure::Cancelled);
	OFXIC_REQUIRE(captured.timeoutSeconds == 11);
	OFXIC_REQUIRE(captured.shouldCancel && captured.shouldCancel());
}

ofxIC::HttpResponse response(int status, std::string body) {
	ofxIC::HttpResponse result;
	result.started = true;
	result.status = status;
	result.body = std::move(body);
	return result;
}

} // namespace

OFXIC_TEST(acestep_music_generates_direct_local_audio) {
	std::vector<ofxIC::HttpRequest> captured;
	ofxIC::Endpoint endpoint("http://127.0.0.1:8085", [&](const ofxIC::HttpRequest & request) {
		captured.push_back(request);
		if (request.url.find("/lm") != std::string::npos) {
			return response(200, R"({"result":{"caption":"enriched local music"}})");
		}
		return response(200, wavBytes());
	});
	endpoint.setBearerToken("must-not-leak-to-local-server");
	ofxIC::AceStepMusicClient music(endpoint);
	ofxIC::AceStepMusicRequest request;
	request.caption = "Warm local ambient pulse";
	request.durationSeconds = 8;
	request.seed = 42;
	request.outputFormat = "wav";

	const auto completed = music.submit(request);
	OFXIC_REQUIRE(completed);
	OFXIC_REQUIRE(completed.state == ofxIC::AceStepMusicJobState::Completed);
	OFXIC_REQUIRE(completed.outputFormat == "wav");
	OFXIC_REQUIRE(completed.audioBytes == wavBytes());
	OFXIC_REQUIRE(captured.size() == 2);
	OFXIC_REQUIRE(captured[0].url == "http://127.0.0.1:8085/lm");
	OFXIC_REQUIRE(captured[0].body.find("\"caption\":\"Warm local ambient pulse\"") != std::string::npos);
	OFXIC_REQUIRE(captured[0].body.find("\"lyrics\":\"[Instrumental]\"") != std::string::npos);
	OFXIC_REQUIRE(captured[0].body.find("\"duration\":8") != std::string::npos);
	OFXIC_REQUIRE(captured[0].body.find("\"seed\":42") != std::string::npos);
	OFXIC_REQUIRE(captured[0].headers.empty());
	OFXIC_REQUIRE(captured[1].url == "http://127.0.0.1:8085/synth");
	OFXIC_REQUIRE(captured[1].accept == "audio/wav");
	OFXIC_REQUIRE(captured[1].body.find("\"caption\":\"enriched local music\"") != std::string::npos);
	OFXIC_REQUIRE(captured[1].body.find("\"output_format\":\"wav16\"") != std::string::npos);
	OFXIC_REQUIRE(captured[1].headers.empty());
}

OFXIC_TEST(acestep_music_advances_async_lm_and_synth_jobs) {
	std::vector<ofxIC::HttpRequest> captured;
	int call = 0;
	ofxIC::Endpoint endpoint("http://127.0.0.1:8085/", [&](const ofxIC::HttpRequest & request) {
		captured.push_back(request);
		switch (call++) {
		case 0: return response(200, R"({"id":"lm_job-1"})");
		case 1: return response(200, R"({"status":"running"})");
		case 2: return response(200, R"({"status":"done"})");
		case 3: return response(200, R"({"caption":"enriched async music"})");
		case 4: return response(202, R"({"id":"synth_job-2"})");
		case 5: return response(200, R"({"status":"done"})");
		default: return response(200, wavBytes());
		}
	});
	ofxIC::AceStepMusicClient music(endpoint);
	ofxIC::AceStepMusicRequest request;
	request.caption = "Async local music";
	request.outputFormat = "wav";

	const auto lmSubmitted = music.submit(request);
	const auto lmGenerating = music.poll(lmSubmitted);
	const auto synthSubmitted = music.poll(lmGenerating);
	const auto completed = music.poll(synthSubmitted);
	OFXIC_REQUIRE(lmSubmitted);
	OFXIC_REQUIRE(lmSubmitted.phase == ofxIC::AceStepMusicJobPhase::LanguageModel);
	OFXIC_REQUIRE(lmSubmitted.id == "lm_job-1");
	OFXIC_REQUIRE(lmGenerating.state == ofxIC::AceStepMusicJobState::Generating);
	OFXIC_REQUIRE(synthSubmitted);
	OFXIC_REQUIRE(synthSubmitted.phase == ofxIC::AceStepMusicJobPhase::Synthesis);
	OFXIC_REQUIRE(synthSubmitted.id == "synth_job-2");
	OFXIC_REQUIRE(completed);
	OFXIC_REQUIRE(completed.state == ofxIC::AceStepMusicJobState::Completed);
	OFXIC_REQUIRE(completed.audioBytes == wavBytes());
	OFXIC_REQUIRE(captured.size() == 7);
	OFXIC_REQUIRE(captured[1].url.find("/job?id=lm_job-1") != std::string::npos);
	OFXIC_REQUIRE(captured[3].url.find("/job?id=lm_job-1&result=1") != std::string::npos);
	OFXIC_REQUIRE(captured[4].url.find("/synth") != std::string::npos);
	OFXIC_REQUIRE(captured[6].url.find("/job?id=synth_job-2&result=1") != std::string::npos);
}

OFXIC_TEST(acestep_music_validates_before_transport) {
	int calls = 0;
	ofxIC::Endpoint endpoint("http://127.0.0.1:8085", [&](const ofxIC::HttpRequest &) {
		++calls;
		return ofxIC::HttpResponse{};
	});
	ofxIC::AceStepMusicClient music(endpoint);
	ofxIC::AceStepMusicRequest request;
	request.caption = "test";
	request.durationSeconds = 0;
	const auto failed = music.submit(request);
	OFXIC_REQUIRE(!failed);
	OFXIC_REQUIRE(failed.error.find("duration") != std::string::npos);
	OFXIC_REQUIRE(calls == 0);
}

OFXIC_TEST(acestep_music_rejects_non_audio_synthesis_success) {
	int call = 0;
	ofxIC::Endpoint endpoint("http://127.0.0.1:8085", [&](const ofxIC::HttpRequest &) {
		if (call++ == 0) return response(200, R"({"caption":"ready"})");
		return response(200, R"({"message":"not audio"})");
	});
	ofxIC::AceStepMusicClient music(endpoint);
	ofxIC::AceStepMusicRequest request;
	request.caption = "test";
	const auto failed = music.submit(request);
	OFXIC_REQUIRE(!failed);
	OFXIC_REQUIRE(failed.error.find("valid wav audio") != std::string::npos);
}

OFXIC_TEST(acestep_music_extracts_audio_from_multipart_synthesis) {
	const std::string boundary = "ofxic-acestep-boundary";
	const std::string multipart = "--" + boundary + "\r\n"
		"Content-Type: application/json\r\n\r\n{}\r\n"
		"--" + boundary + "\r\n"
		"Content-Type: audio/wav\r\n"
		"Content-Disposition: attachment; filename=music.wav\r\n\r\n" +
		wavBytes() + "\r\n--" + boundary + "--\r\n";
	int call = 0;
	ofxIC::Endpoint endpoint("http://127.0.0.1:8085", [&](const ofxIC::HttpRequest &) {
		if (call++ == 0) return response(200, R"({"caption":"ready"})");
		return response(200, multipart);
	});
	ofxIC::AceStepMusicClient music(endpoint);
	ofxIC::AceStepMusicRequest request;
	request.caption = "multipart";
	const auto completed = music.submit(request);
	OFXIC_REQUIRE(completed);
	OFXIC_REQUIRE(completed.state == ofxIC::AceStepMusicJobState::Completed);
	OFXIC_REQUIRE(completed.audioBytes == wavBytes());
}

OFXIC_TEST(acestep_music_rejects_job_status_without_state) {
	int call = 0;
	ofxIC::Endpoint endpoint("http://127.0.0.1:8085", [&](const ofxIC::HttpRequest &) {
		if (call++ == 0) return response(200, R"({"id":"job-1"})");
		return response(200, R"({"message":"missing status"})");
	});
	ofxIC::AceStepMusicClient music(endpoint);
	ofxIC::AceStepMusicRequest request;
	request.caption = "invalid status";
	const auto submitted = music.submit(request);
	const auto failed = music.poll(submitted);
	OFXIC_REQUIRE(!failed);
	OFXIC_REQUIRE(failed.error.find("invalid status JSON") != std::string::npos);
}
