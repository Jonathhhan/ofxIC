#include "test_harness.h"
#include "../src/ofxIC.h"

#include <string>
#include <vector>

namespace {

const std::string jobId(64, 'a');

} // namespace

OFXIC_TEST(stability_audio_submits_stable_audio_3_job) {
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("https://api.stability.ai", [&](const ofxIC::HttpRequest & request) {
		captured = request;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 202;
		response.body = "{\"id\":\"" + jobId + "\"}";
		return response;
	});
	endpoint.setBearerToken("stability-test-token");
	ofxIC::StabilityAudioClient audio(endpoint);
	ofxIC::StabilityAudioRequest request;
	request.prompt = "Warm modular synthesizer, no vocals";
	request.durationSeconds = 17;
	request.seed = 42;
	request.steps = 6;
	request.guidance = 2.5f;
	request.outputFormat = "wav";

	const auto submitted = audio.submit(request);
	OFXIC_REQUIRE(submitted);
	OFXIC_REQUIRE(submitted.state == ofxIC::StabilityAudioJobState::Submitted);
	OFXIC_REQUIRE(submitted.id == jobId);
	OFXIC_REQUIRE(submitted.outputFormat == "wav");
	OFXIC_REQUIRE(submitted.mimeType == "audio/wav");
	OFXIC_REQUIRE(captured.method == ofxIC::HttpMethod::Post);
	OFXIC_REQUIRE(captured.url == "https://api.stability.ai/v2beta/audio/stable-audio/text-to-audio");
	OFXIC_REQUIRE(captured.contentType.find("multipart/form-data; boundary=") == 0);
	OFXIC_REQUIRE(captured.accept == "application/json");
	OFXIC_REQUIRE(captured.body.find("name=\"prompt\"") != std::string::npos);
	OFXIC_REQUIRE(captured.body.find(request.prompt) != std::string::npos);
	OFXIC_REQUIRE(captured.body.find("name=\"duration\"\r\n\r\n17") != std::string::npos);
	OFXIC_REQUIRE(captured.body.find("name=\"cfg_scale\"\r\n\r\n2.5") != std::string::npos);
	OFXIC_REQUIRE(captured.body.find("name=\"output_format\"\r\n\r\nwav") != std::string::npos);
	OFXIC_REQUIRE(captured.headers.size() == 1);
}

OFXIC_TEST(stability_audio_forwards_control_and_classifies_timeout) {
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("https://api.stability.ai", [&](const ofxIC::HttpRequest & request) {
		captured = request;
		ofxIC::HttpResponse response;
		response.failure = ofxIC::RequestFailure::Timeout;
		response.error = "request timed out";
		return response;
	});
	ofxIC::StabilityAudioClient audio(endpoint);
	ofxIC::StabilityAudioRequest request;
	request.prompt = "bounded music";
	ofxIC::RequestControl control;
	control.timeoutSeconds = 9;

	const auto result = audio.submit(request, control);
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.failure == ofxIC::RequestFailure::Timeout);
	OFXIC_REQUIRE(captured.timeoutSeconds == 9);
}

OFXIC_TEST(stability_audio_polls_until_mp3_is_complete) {
	std::vector<ofxIC::HttpRequest> captured;
	int calls = 0;
	ofxIC::Endpoint endpoint("https://api.stability.ai/", [&](const ofxIC::HttpRequest & request) {
		captured.push_back(request);
		ofxIC::HttpResponse response;
		response.started = true;
		if (calls++ == 0) {
			response.status = 202;
			response.body = R"({"status":"in-progress"})";
		} else {
			response.status = 200;
			response.body = std::string("ID3\x04\0\0music", 11);
		}
		return response;
	});
	ofxIC::StabilityAudioClient audio(endpoint);
	ofxIC::StabilityAudioJob submitted;
	submitted.success = true;
	submitted.state = ofxIC::StabilityAudioJobState::Submitted;
	submitted.id = jobId;
	submitted.outputFormat = "mp3";

	const auto generating = audio.poll(submitted);
	const auto completed = audio.poll(generating);
	OFXIC_REQUIRE(generating);
	OFXIC_REQUIRE(generating.state == ofxIC::StabilityAudioJobState::Generating);
	OFXIC_REQUIRE(completed);
	OFXIC_REQUIRE(completed.state == ofxIC::StabilityAudioJobState::Completed);
	OFXIC_REQUIRE(completed.audioBytes.size() == 11);
	OFXIC_REQUIRE(completed.rawResponse.empty());
	OFXIC_REQUIRE(captured.size() == 2);
	OFXIC_REQUIRE(captured[0].url == "https://api.stability.ai/v2beta/audio/results/" + jobId);
	OFXIC_REQUIRE(captured[0].accept == "audio/*");
}

OFXIC_TEST(stability_audio_validates_requests_before_transport) {
	int calls = 0;
	ofxIC::Endpoint endpoint("https://api.stability.ai", [&](const ofxIC::HttpRequest &) {
		++calls;
		return ofxIC::HttpResponse{};
	});
	ofxIC::StabilityAudioClient audio(endpoint);
	ofxIC::StabilityAudioRequest request;
	request.prompt = "test";
	request.durationSeconds = 381;
	const auto failed = audio.submit(request);
	OFXIC_REQUIRE(!failed);
	OFXIC_REQUIRE(failed.state == ofxIC::StabilityAudioJobState::Failed);
	OFXIC_REQUIRE(failed.error.find("duration") != std::string::npos);
	OFXIC_REQUIRE(calls == 0);
}

OFXIC_TEST(stability_audio_surfaces_provider_error_detail) {
	ofxIC::Endpoint endpoint("https://api.stability.ai", [](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 402;
		response.body = R"({"message":"insufficient credits"})";
		return response;
	});
	ofxIC::StabilityAudioClient audio(endpoint);
	ofxIC::StabilityAudioRequest request;
	request.prompt = "test";
	const auto failed = audio.submit(request);
	OFXIC_REQUIRE(!failed);
	OFXIC_REQUIRE(failed.httpStatus == 402);
	OFXIC_REQUIRE(failed.error.find("insufficient credits") != std::string::npos);
}

OFXIC_TEST(stability_audio_rejects_non_audio_success_payload) {
	ofxIC::Endpoint endpoint("https://api.stability.ai", [](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"message":"not audio"})";
		return response;
	});
	ofxIC::StabilityAudioClient audio(endpoint);
	ofxIC::StabilityAudioJob job;
	job.success = true;
	job.id = jobId;
	job.outputFormat = "wav";
	const auto failed = audio.poll(job);
	OFXIC_REQUIRE(!failed);
	OFXIC_REQUIRE(failed.error.find("valid wav audio") != std::string::npos);
}
