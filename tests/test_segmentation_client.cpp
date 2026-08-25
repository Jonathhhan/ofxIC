#include "test_harness.h"
#include "../src/ofxIC.h"

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
	OFXIC_REQUIRE(captured.body.find("name=\"image\"") != std::string::npos);
	OFXIC_REQUIRE(captured.body.find("0.25,0.75,positive") != std::string::npos);
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
