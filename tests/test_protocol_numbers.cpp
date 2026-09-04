#include "test_harness.h"
#include "../src/ofxIC.h"

#include <limits>
#include <locale>
#include <string>
#include <utility>

namespace {
class CommaPunctuation : public std::numpunct<char> {
	char do_decimal_point() const override { return ','; }
	char do_thousands_sep() const override { return '.'; }
	std::string do_grouping() const override { return "\3"; }
};

class ScopedCommaLocale {
public:
	// The test runner executes cases sequentially; restore the process locale
	// even if an assertion throws. No installed system locale is required.
	ScopedCommaLocale() : previous(std::locale()) {
		std::locale::global(std::locale(std::locale::classic(), new CommaPunctuation));
	}
	~ScopedCommaLocale() { std::locale::global(previous); }
private:
	std::locale previous;
};

ofxIC::HttpResponse jsonResponse(std::string body) {
	ofxIC::HttpResponse response;
	response.started = true;
	response.status = 200;
	response.body = std::move(body);
	return response;
}

const float nonFinite[] = { std::numeric_limits<float>::quiet_NaN(),
	std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
} // namespace

OFXIC_TEST(protocol_chat_numbers_ignore_application_locale) {
	ScopedCommaLocale locale;
	std::string body;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest & request) {
		body = request.body;
		return jsonResponse(R"({"choices":[{"message":{"content":"ok"}}]})");
	});
	ofxIC::ChatRequest request;
	request.messages.push_back({ ofxIC::ChatRole::User, "hello" });
	request.options.maxTokens = 2048;
	request.options.temperature = 0.75f;
	request.options.topP = 0.95f;
	OFXIC_REQUIRE(endpoint.chat(request));
	OFXIC_REQUIRE(body.find("\"max_tokens\":2048") != std::string::npos);
	OFXIC_REQUIRE(body.find("\"temperature\":0.75") != std::string::npos);
	OFXIC_REQUIRE(body.find("\"top_p\":0.95") != std::string::npos);
}

OFXIC_TEST(protocol_image_dimensions_ignore_application_locale) {
	ScopedCommaLocale locale;
	std::string body;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest & request) {
		body = request.body;
		return jsonResponse(R"({"data":[{"b64_json":"aW1hZ2U="}]})");
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::ImageRequest request;
	request.prompt = "image";
	OFXIC_REQUIRE(media.generateImage(request));
	OFXIC_REQUIRE(body.find("\"size\":\"1024x1024\"") != std::string::npos);
}

OFXIC_TEST(protocol_native_media_numbers_ignore_application_locale) {
	ScopedCommaLocale locale;
	std::string body;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest & request) {
		if (request.method == ofxIC::HttpMethod::Get)
			return jsonResponse(R"({"supported_modes":["img_gen"]})");
		body = request.body;
		return jsonResponse(R"({"id":"one","status":"queued"})");
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::MediaJobRequest request;
	request.prompt = "image";
	request.guidance = 7.5f;
	OFXIC_REQUIRE(media.submit(request));
	OFXIC_REQUIRE(body.find("\"width\":1024") != std::string::npos);
	OFXIC_REQUIRE(body.find("\"txt_cfg\":7.5") != std::string::npos);
}

OFXIC_TEST(protocol_fal_numbers_ignore_application_locale) {
	ScopedCommaLocale locale;
	std::string body;
	ofxIC::Endpoint endpoint("https://router.huggingface.co", [&](const ofxIC::HttpRequest & request) {
		if (request.method == ofxIC::HttpMethod::Get)
			return jsonResponse(R"({"inferenceProviderMapping":{"fal-ai":{"status":"live","providerId":"fal-ai/flux/dev","task":"text-to-image"}}})");
		body = request.body;
		return jsonResponse(R"({"request_id":"one","status":"IN_QUEUE"})");
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::MediaJobRequest request;
	request.prompt = "image";
	request.model = "test/model";
	request.guidance = 7.5f;
	media.submitHuggingFaceFal(request);
	OFXIC_REQUIRE(body.find("\"width\":1024") != std::string::npos);
	OFXIC_REQUIRE(body.find("\"guidance_scale\":7.5") != std::string::npos);
}

OFXIC_TEST(protocol_sam_coordinates_ignore_application_locale) {
	ScopedCommaLocale locale;
	std::string body;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest & request) {
		body = request.body;
		return jsonResponse("P5\n1 1\n255\nX");
	});
	ofxIC::SegmentationClient client(endpoint);
	ofxIC::SegmentationRequest request;
	request.imageBytes = "ppm";
	request.points.push_back({ 0.25f, 0.75f, true });
	OFXIC_REQUIRE(client.segmentSamBridge(request));
	OFXIC_REQUIRE(body.find("0.25,0.75,positive") != std::string::npos);
}

OFXIC_TEST(protocol_document_scores_ignore_application_locale) {
	ScopedCommaLocale locale;
	ofxIC::DocumentIndex index;
	index.addText("guide", "needle");
	ofxIC::ToolRegistry registry;
	registry.addDocumentSearch(index);
	const auto result = registry.execute({ "one", "search_documents", R"({"query":"needle"})" });
	OFXIC_REQUIRE(result);
	OFXIC_REQUIRE(result.content.find("\"score\":2.25") != std::string::npos);
}

OFXIC_TEST(protocol_chat_rejects_nonfinite_sampling_before_transport) {
	int calls = 0;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest &) {
		++calls;
		return ofxIC::HttpResponse{};
	});
	for (const float value : nonFinite) {
		for (const bool temperature : { false, true }) {
			ofxIC::ChatRequest request;
			request.messages.push_back({ ofxIC::ChatRole::User, "hello" });
			if (temperature) request.options.temperature = value;
			else request.options.topP = value;
			const auto result = endpoint.chat(request);
			OFXIC_REQUIRE(!result);
			OFXIC_REQUIRE(result.failure == ofxIC::RequestFailure::Validation);
		}
	}
	OFXIC_REQUIRE(calls == 0);
}

OFXIC_TEST(protocol_media_rejects_nonfinite_guidance_before_capabilities_or_mapping) {
	int calls = 0;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest &) {
		++calls;
		return ofxIC::HttpResponse{};
	});
	ofxIC::MediaClient media(endpoint);
	for (const float value : nonFinite) {
		ofxIC::MediaJobRequest request;
		request.prompt = "image";
		request.model = "test/model";
		request.guidance = value;
		OFXIC_REQUIRE(media.submit(request).failure == ofxIC::RequestFailure::Validation);
		OFXIC_REQUIRE(media.submitHuggingFaceFal(request).failure == ofxIC::RequestFailure::Validation);
	}
	OFXIC_REQUIRE(calls == 0);
}

OFXIC_TEST(protocol_sam_rejects_nonfinite_coordinates_before_transport) {
	int calls = 0;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest &) {
		++calls;
		return ofxIC::HttpResponse{};
	});
	ofxIC::SegmentationClient client(endpoint);
	for (const float value : nonFinite) {
		for (const bool x : { false, true }) {
			ofxIC::SegmentationRequest request;
			request.imageBytes = "ppm";
			request.points.push_back({ x ? value : 0.5f, x ? 0.5f : value, true });
			const auto result = client.segmentSamBridge(request);
			OFXIC_REQUIRE(!result);
			OFXIC_REQUIRE(result.failure == ofxIC::RequestFailure::Validation);
		}
	}
	OFXIC_REQUIRE(calls == 0);
}
