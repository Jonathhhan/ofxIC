#include "test_harness.h"
#include "../src/ofxIC.h"

OFXIC_TEST(transcription_client_builds_openai_multipart_request) {
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("https://api.openai.com/v1", [&](const ofxIC::HttpRequest & request) {
		captured = request;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"text":"hello world"})";
		return response;
	});
	ofxIC::TranscriptionClient client(endpoint);
	ofxIC::TranscriptionRequest request;
	request.audioBytes = std::string("RIFF\0audio", 10);
	request.filename = "sample.wav";
	request.model = "whisper-1";
	request.language = "en";

	const auto result = client.transcribeOpenAI(request);
	OFXIC_REQUIRE(result.text == "hello world");
	OFXIC_REQUIRE(captured.url == "https://api.openai.com/v1/audio/transcriptions");
	OFXIC_REQUIRE(captured.contentType.find("multipart/form-data; boundary=") == 0);
	OFXIC_REQUIRE(captured.body.find("name=\"file\"; filename=\"sample.wav\"") != std::string::npos);
	OFXIC_REQUIRE(captured.body.find("name=\"model\"") != std::string::npos);
	OFXIC_REQUIRE(captured.body.find("whisper-1") != std::string::npos);
	OFXIC_REQUIRE(captured.body.find(std::string("RIFF\0audio", 10)) != std::string::npos);
}

OFXIC_TEST(transcription_client_keeps_whisper_cpp_protocol_explicit) {
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("http://127.0.0.1:8080", [&](const ofxIC::HttpRequest & request) {
		captured = request;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"text":"local transcript"})";
		return response;
	});
	ofxIC::TranscriptionClient client(endpoint);
	ofxIC::TranscriptionRequest request;
	request.audioBytes = "audio";
	request.model = "must-not-be-uploaded";

	OFXIC_REQUIRE(client.transcribeWhisperCpp(request));
	OFXIC_REQUIRE(captured.url == "http://127.0.0.1:8080/inference");
	OFXIC_REQUIRE(captured.body.find("must-not-be-uploaded") == std::string::npos);
}

OFXIC_TEST(transcription_client_rejects_empty_audio_before_transport) {
	int calls = 0;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest &) {
		++calls;
		return ofxIC::HttpResponse{};
	});
	ofxIC::TranscriptionClient client(endpoint);
	const auto result = client.transcribeOpenAI({});
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(calls == 0);
}

OFXIC_TEST(transcription_client_reports_cancellation_separately) {
	ofxIC::Endpoint endpoint("http://127.0.0.1:8080", [](const ofxIC::HttpRequest & request) {
		ofxIC::HttpResponse response;
		response.started = true;
		response.cancelled = request.shouldCancel && request.shouldCancel();
		response.error = response.cancelled ? "request cancelled" : "";
		return response;
	});
	ofxIC::TranscriptionClient client(endpoint);
	ofxIC::TranscriptionRequest request;
	request.audioBytes = "audio";

	const auto result = client.transcribeOpenAI(request, []() { return true; });
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.cancelled);
	OFXIC_REQUIRE(result.error == "request cancelled");
}
