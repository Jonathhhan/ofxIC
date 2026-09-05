#include "test_harness.h"
#include "../src/ofxIC.h"

OFXIC_TEST(chat_uses_protocol_fields_and_rejects_malformed_documents) {
	std::string body;
	ofxIC::Endpoint endpoint("https://example.test", [&](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true; response.status = 200; response.body = body;
		return response;
	});
	ofxIC::ChatRequest request;
	request.messages.push_back({ofxIC::ChatRole::User, "hi"});
	body = R"({"metadata":{"content":"wrong"},"choices":[{"message":{"content":"right"}}]})";
	OFXIC_REQUIRE(endpoint.chat(request).text == "right");
	for (const std::string invalid : {
		R"({"choices":[{"message":{"content":"partial"}}])",
		R"({"content":"one","content":"two"})",
		R"({"content":"ok"} trailing)",
		R"({"choices":[],"metadata":{"content":"wrong"}})",
		R"({"metadata":{"tool_calls":[{"id":"x","type":"function","function":{"name":"search_documents","arguments":"{}"}}]}})"
	}) {
		body = invalid;
		OFXIC_REQUIRE(!endpoint.chat(request));
	}
	body = R"({"metadata":{"id":"wrong"},"data":[{"id":"actual"}]})";
	OFXIC_REQUIRE(endpoint.inspect().models == std::vector<std::string>{"actual"});
}

OFXIC_TEST(audio_and_sam_preserve_failures_after_success_headers) {
	ofxIC::HttpResponse response;
	response.started = true; response.status = 200;
	response.failure = ofxIC::RequestFailure::Timeout;
	response.error = "body timed out";
	response.body = R"({"text":"partial","status":"ok","version":"1","mode":"runner"})";
	ofxIC::Endpoint endpoint("https://example.test", [&](const ofxIC::HttpRequest &) { return response; });
	ofxIC::TranscriptionClient speech(endpoint);
	ofxIC::TranscriptionRequest transcription;
	transcription.audioBytes = "audio";
	const auto transcript = speech.transcribeWhisperCpp(transcription);
	OFXIC_REQUIRE(!transcript);
	OFXIC_REQUIRE(transcript.failure == ofxIC::RequestFailure::Timeout);
	ofxIC::SegmentationClient sam(endpoint);
	OFXIC_REQUIRE(!sam.inspectSamBridge());
	ofxIC::SegmentationRequest segmentation;
	segmentation.imageBytes = "P6\n1 1\n255\nRGB";
	segmentation.points.push_back({0.5f, 0.5f, true});
	OFXIC_REQUIRE(sam.segmentSamBridge(segmentation).failure == ofxIC::RequestFailure::Timeout);
	ofxIC::StabilityAudioClient audio(endpoint);
	ofxIC::StabilityAudioRequest music;
	music.prompt = "music";
	response.status = 202;
	response.body = "{\"id\":\"" + std::string(64, 'a') + "\"}";
	OFXIC_REQUIRE(audio.submit(music).failure == ofxIC::RequestFailure::Timeout);
	ofxIC::StabilityAudioJob job;
	job.id = std::string(64, 'a'); job.outputFormat = "wav";
	OFXIC_REQUIRE(audio.poll(job).failure == ofxIC::RequestFailure::Timeout);
	ofxIC::AceStepMusicClient ace(endpoint);
	ofxIC::AceStepMusicRequest aceRequest;
	aceRequest.caption = "music";
	OFXIC_REQUIRE(ace.submit(aceRequest).failure == ofxIC::RequestFailure::Timeout);
}

OFXIC_TEST(image_download_is_bounded_token_free_and_validates_payload) {
	ofxIC::HttpRequest captured;
	ofxIC::HttpResponse response;
	response.started = true; response.status = 200;
	response.body = std::string("\x89PNG\r\n\x1a\n", 8) + "fixture";
	ofxIC::Endpoint endpoint("https://provider.test", [&](const ofxIC::HttpRequest & request) {
		captured = request; return response;
	});
	endpoint.setBearerToken("private");
	ofxIC::MediaClient media(endpoint);
	const auto image = media.downloadImage("https://cdn.test/generated");
	OFXIC_REQUIRE(image);
	OFXIC_REQUIRE(image.outputFormat == "png");
	OFXIC_REQUIRE(image.imageBytes == response.body);
	OFXIC_REQUIRE(captured.headers.empty());
	OFXIC_REQUIRE(captured.maxResponseBytes == 64U * 1024U * 1024U);
	response.body = "<html>error</html>";
	OFXIC_REQUIRE(!media.downloadImage("https://cdn.test/generated"));
	response.failure = ofxIC::RequestFailure::Timeout;
	OFXIC_REQUIRE(media.downloadImage("https://cdn.test/generated").failure == ofxIC::RequestFailure::Timeout);
}
