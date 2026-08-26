#include "test_harness.h"
#include "../src/ofxIC.h"

OFXIC_TEST(segmentation_client_inspects_bridge_health) {
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("http://127.0.0.1:18085", [&](const ofxIC::HttpRequest & request) {
		captured = request;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"status":"ok","version":"1","mode":"runner","backend":"cuda"})";
		return response;
	});
	ofxIC::SegmentationClient client(endpoint);
	const auto health = client.inspectSamBridge();
	OFXIC_REQUIRE(health);
	OFXIC_REQUIRE(health.version == "1");
	OFXIC_REQUIRE(health.mode == "runner");
	OFXIC_REQUIRE(health.backend == "cuda");
	OFXIC_REQUIRE(captured.method == ofxIC::HttpMethod::Get);
	OFXIC_REQUIRE(captured.url == "http://127.0.0.1:18085/health");
	OFXIC_REQUIRE(captured.maxResponseBytes == 16U * 1024U);
	OFXIC_REQUIRE(captured.headers.size() == 1);
}

OFXIC_TEST(segmentation_client_rejects_incompatible_bridge_health) {
	ofxIC::Endpoint endpoint("http://example.test", [](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"status":"ok","version":"2"})";
		return response;
	});
	ofxIC::SegmentationClient client(endpoint);
	const auto health = client.inspectSamBridge();
	OFXIC_REQUIRE(!health);
	OFXIC_REQUIRE(health.error.find("incompatible") != std::string::npos);
}

OFXIC_TEST(segmentation_client_builds_explicit_sam_bridge_request) {
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("http://127.0.0.1:8090", [&](const ofxIC::HttpRequest & request) {
		captured = request;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = std::string("P5\n1 1\n255\n\xff", 12);
		return response;
	});
	ofxIC::SegmentationClient client(endpoint);
	ofxIC::SegmentationRequest request;
	request.imageBytes = std::string("P6\n1 1\n255\n\x10\x20\x30", 14);
	request.points.push_back({ 0.25f, 0.75f, true });

	const auto result = client.segmentSamBridge(request);
	OFXIC_REQUIRE(result);
	OFXIC_REQUIRE(captured.url == "http://127.0.0.1:8090/v1/segmentations");
	OFXIC_REQUIRE(captured.accept == "image/x-portable-graymap");
	OFXIC_REQUIRE(captured.headers.size() == 1);
	OFXIC_REQUIRE(captured.headers[0].first == "X-ofxIC-SAM-Bridge-Version");
	OFXIC_REQUIRE(captured.headers[0].second == "1");
	OFXIC_REQUIRE(captured.body.find("name=\"image\"") != std::string::npos);
	OFXIC_REQUIRE(captured.body.find("0.25,0.75,positive") != std::string::npos);
}

OFXIC_TEST(segmentation_client_rejects_too_many_prompts_before_transport) {
	int calls = 0;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest &) {
		++calls;
		return ofxIC::HttpResponse{};
	});
	ofxIC::SegmentationClient client(endpoint);
	ofxIC::SegmentationRequest request;
	request.imageBytes = "ppm";
	request.points.resize(65);
	const auto result = client.segmentSamBridge(request);
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.error.find("64 points") != std::string::npos);
	OFXIC_REQUIRE(calls == 0);
}

OFXIC_TEST(segmentation_client_preserves_bridge_problem_details) {
	ofxIC::Endpoint endpoint("http://example.test", [](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 504;
		response.body = "runner_timeout: SAM adapter exceeded its timeout";
		return response;
	});
	ofxIC::SegmentationClient client(endpoint);
	ofxIC::SegmentationRequest request;
	request.imageBytes = "ppm";
	request.points.push_back({});
	const auto result = client.segmentSamBridge(request);
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.httpStatus == 504);
	OFXIC_REQUIRE(result.error.find("runner_timeout") != std::string::npos);
}

OFXIC_TEST(segmentation_client_rejects_invalid_prompt_before_transport) {
	int calls = 0;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest &) {
		++calls;
		return ofxIC::HttpResponse{};
	});
	ofxIC::SegmentationClient client(endpoint);
	ofxIC::SegmentationRequest request;
	request.imageBytes = "ppm";
	request.points.push_back({ 1.5f, 0.5f, true });
	OFXIC_REQUIRE(!client.segmentSamBridge(request));
	OFXIC_REQUIRE(calls == 0);
}

OFXIC_TEST(segmentation_bridge_inspection_forwards_control_and_timeout) {
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest & request) {
		captured = request;
		ofxIC::HttpResponse response;
		response.failure = ofxIC::RequestFailure::Timeout;
		response.error = "request timed out";
		return response;
	});
	ofxIC::SegmentationClient client(endpoint);
	ofxIC::RequestControl control;
	control.timeoutSeconds = 4;
	control.shouldCancel = [] { return false; };

	const auto status = client.inspectSamBridge(control);
	OFXIC_REQUIRE(!status);
	OFXIC_REQUIRE(status.failure == ofxIC::RequestFailure::Timeout);
	OFXIC_REQUIRE(captured.timeoutSeconds == 4);
	OFXIC_REQUIRE(captured.shouldCancel && !captured.shouldCancel());
}

OFXIC_TEST(segmentation_request_forwards_control_and_cancellation) {
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest & request) {
		captured = request;
		ofxIC::HttpResponse response;
		response.cancelled = true;
		response.failure = ofxIC::RequestFailure::Cancelled;
		response.error = "request cancelled";
		return response;
	});
	ofxIC::SegmentationClient client(endpoint);
	ofxIC::SegmentationRequest request;
	request.imageBytes = "ppm";
	request.points.push_back({});
	ofxIC::RequestControl control;
	control.timeoutSeconds = 6;
	control.shouldCancel = [] { return true; };

	const auto result = client.segmentSamBridge(request, control);
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.cancelled);
	OFXIC_REQUIRE(result.failure == ofxIC::RequestFailure::Cancelled);
	OFXIC_REQUIRE(captured.timeoutSeconds == 6);
	OFXIC_REQUIRE(captured.shouldCancel && captured.shouldCancel());
}

OFXIC_TEST(segmentation_rejects_negative_timeout_before_transport) {
	int calls = 0;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest &) {
		++calls;
		return ofxIC::HttpResponse{};
	});
	ofxIC::SegmentationClient client(endpoint);
	ofxIC::RequestControl control;
	control.timeoutSeconds = -1;
	const auto status = client.inspectSamBridge(control);
	OFXIC_REQUIRE(!status);
	OFXIC_REQUIRE(status.failure == ofxIC::RequestFailure::InvalidResponse);
	OFXIC_REQUIRE(calls == 0);
}
