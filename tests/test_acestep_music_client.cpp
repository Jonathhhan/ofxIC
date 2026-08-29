#include "test_harness.h"
#include "../src/ofxIC.h"

#include <string>
#include <utility>
#include <vector>

namespace {

std::string wavBytes() {
	return std::string("RIFF", 4) + std::string(4, '\0') + "WAVEfmt ";
}

ofxIC::HttpResponse response(int status, std::string body) {
	ofxIC::HttpResponse result;
	result.started = true;
	result.status = status;
	result.body = std::move(body);
	return result;
}

} // namespace

OFXIC_TEST(acestep_music_uses_official_15_task_api_and_downloads_audio) {
	std::vector<ofxIC::HttpRequest> captured;
	int call = 0;
	ofxIC::Endpoint endpoint("http://127.0.0.1:8085", [&](const ofxIC::HttpRequest & request) {
		captured.push_back(request);
		switch (call++) {
		case 0: return response(200, R"({"data":{"task_id":"task-42","status":"queued"},"code":200,"error":null})");
		case 1: return response(200, R"({"data":[{"task_id":"task-42","status":0,"result":"[]"}],"code":200,"error":null})");
		case 2: return response(200, R"({"data":[{"task_id":"task-42","status":1,"result":"[{\"file\":\"/v1/audio?path=%2Ftmp%2Fmusic.wav\"}]"}],"code":200,"error":null})");
		default: return response(200, wavBytes());
		}
	});
	endpoint.setBearerToken("must-not-leak-to-local-server");
	ofxIC::AceStepMusicClient music(endpoint);
	ofxIC::AceStepMusicRequest request;
	request.caption = "Warm local ambient pulse";
	request.durationSeconds = 30;
	request.seed = 42;
	request.outputFormat = "wav";

	const auto submitted = music.submit(request);
	const auto generating = music.poll(submitted);
	const auto completed = music.poll(generating);
	OFXIC_REQUIRE(submitted && submitted.id == "task-42");
	OFXIC_REQUIRE(submitted.phase == ofxIC::AceStepMusicJobPhase::Synthesis);
	OFXIC_REQUIRE(generating && generating.state == ofxIC::AceStepMusicJobState::Generating);
	OFXIC_REQUIRE(completed && completed.state == ofxIC::AceStepMusicJobState::Completed);
	OFXIC_REQUIRE(completed.audioBytes == wavBytes());
	OFXIC_REQUIRE(captured.size() == 4);
	OFXIC_REQUIRE(captured[0].url == "http://127.0.0.1:8085/release_task");
	OFXIC_REQUIRE(captured[0].body.find("\"prompt\":\"Warm local ambient pulse\"") != std::string::npos);
	OFXIC_REQUIRE(captured[0].body.find("\"audio_duration\":30") != std::string::npos);
	OFXIC_REQUIRE(captured[0].body.find("\"model\":\"acestep-v15-turbo\"") != std::string::npos);
	OFXIC_REQUIRE(captured[0].body.find("\"seed\":42") != std::string::npos);
	OFXIC_REQUIRE(captured[0].headers.empty());
	OFXIC_REQUIRE(captured[1].url == "http://127.0.0.1:8085/query_result");
	OFXIC_REQUIRE(captured[1].method == ofxIC::HttpMethod::Post);
	OFXIC_REQUIRE(captured[1].body == "{\"task_id_list\":[\"task-42\"]}");
	OFXIC_REQUIRE(captured[3].url == "http://127.0.0.1:8085/v1/audio?path=%2Ftmp%2Fmusic.wav");
}

OFXIC_TEST(acestep_music_forwards_control_and_classifies_cancellation) {
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("http://localhost:8085", [&](const ofxIC::HttpRequest & request) {
		captured = request;
		ofxIC::HttpResponse result;
		result.cancelled = true;
		result.failure = ofxIC::RequestFailure::Cancelled;
		result.error = "request cancelled";
		return result;
	});
	ofxIC::AceStepMusicClient music(endpoint);
	ofxIC::AceStepMusicRequest request;
	request.caption = "cancelled music";
	ofxIC::RequestControl control;
	control.timeoutSeconds = 11;
	control.shouldCancel = [] { return true; };
	const auto result = music.submit(request, control);
	OFXIC_REQUIRE(!result && result.cancelled);
	OFXIC_REQUIRE(result.failure == ofxIC::RequestFailure::Cancelled);
	OFXIC_REQUIRE(captured.timeoutSeconds == 11);
	OFXIC_REQUIRE(captured.shouldCancel && captured.shouldCancel());
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
	request.durationSeconds = 9;
	const auto failed = music.submit(request);
	OFXIC_REQUIRE(!failed);
	OFXIC_REQUIRE(failed.error.find("duration") != std::string::npos);
	OFXIC_REQUIRE(calls == 0);
}

OFXIC_TEST(acestep_music_rejects_unsafe_audio_result_url) {
	int call = 0;
	ofxIC::Endpoint endpoint("http://127.0.0.1:8085", [&](const ofxIC::HttpRequest &) {
		if (call++ == 0) return response(200, R"({"data":{"task_id":"job-1"},"code":200,"error":null})");
		return response(200, R"({"data":[{"task_id":"job-1","status":1,"result":"[{\"file\":\"https://attacker.example/audio.wav\"}]"}],"code":200,"error":null})");
	});
	ofxIC::AceStepMusicClient music(endpoint);
	ofxIC::AceStepMusicRequest request;
	request.caption = "safe local download";
	const auto submitted = music.submit(request);
	const auto failed = music.poll(submitted);
	OFXIC_REQUIRE(!failed);
	OFXIC_REQUIRE(failed.error.find("safe /v1/audio") != std::string::npos);
	OFXIC_REQUIRE(call == 2);
}

OFXIC_TEST(acestep_music_reports_official_task_failure) {
	int call = 0;
	ofxIC::Endpoint endpoint("http://127.0.0.1:8085", [&](const ofxIC::HttpRequest &) {
		if (call++ == 0) return response(200, R"({"data":{"task_id":"job-failed"},"code":200,"error":null})");
		return response(200, R"({"data":[{"task_id":"job-failed","status":2,"result":"out of memory"}],"code":200,"error":null})");
	});
	ofxIC::AceStepMusicClient music(endpoint);
	ofxIC::AceStepMusicRequest request;
	request.caption = "failure";
	const auto failed = music.poll(music.submit(request));
	OFXIC_REQUIRE(!failed);
	OFXIC_REQUIRE(failed.error.find("out of memory") != std::string::npos);
}
