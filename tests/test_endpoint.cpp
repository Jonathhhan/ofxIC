#include "test_harness.h"
#include "../src/ofxIC.h"

#include <string>

OFXIC_TEST(endpoint_normalizes_openai_endpoint_urls) {
	ofxIC::HttpRequest captured;
	auto transport = [&](const ofxIC::HttpRequest & request) {
		captured = request;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = "{\"data\":[]}";
		return response;
	};
	ofxIC::Endpoint endpoint(
		"http://localhost:8001/v1/chat/completions", transport);
	ofxIC::Endpoint defaultEndpoint("", transport);
	ofxIC::Endpoint imageEndpoint(
		"http://localhost:8001/v1/images/generations", transport);

	OFXIC_REQUIRE(endpoint.getBaseUrl() == "http://localhost:8001");
	OFXIC_REQUIRE(defaultEndpoint.getBaseUrl() == "http://127.0.0.1:8080");
	OFXIC_REQUIRE(imageEndpoint.getBaseUrl() == "http://localhost:8001");
	OFXIC_REQUIRE(endpoint.inspect());
	OFXIC_REQUIRE(captured.url == "http://localhost:8001/v1/models");
}

OFXIC_TEST(endpoint_inspects_models_endpoint) {
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("http://localhost:8001", [&](const ofxIC::HttpRequest & request) {
		captured = request;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = "{\"data\":[{\"id\":\"local/qwen\"},{\"id\":\"embed\"}]}";
		return response;
	});

	const auto status = endpoint.inspect();
	OFXIC_REQUIRE(status);
	OFXIC_REQUIRE(captured.method == ofxIC::HttpMethod::Get);
	OFXIC_REQUIRE(captured.url == "http://localhost:8001/v1/models");
	OFXIC_REQUIRE(status.models.size() == 2);
	OFXIC_REQUIRE(status.models[0] == "local/qwen");
}

OFXIC_TEST(endpoint_inspection_can_be_cancelled) {
	bool receivedCancellation = false;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest & request) {
		receivedCancellation = request.shouldCancel && request.shouldCancel();
		ofxIC::HttpResponse response;
		response.started = true;
		response.cancelled = receivedCancellation;
		response.error = "request cancelled";
		return response;
	});

	const auto status = endpoint.inspect([]() { return true; });

	OFXIC_REQUIRE(!status);
	OFXIC_REQUIRE(status.cancelled);
	OFXIC_REQUIRE(receivedCancellation);
	OFXIC_REQUIRE(status.failure == ofxIC::RequestFailure::Cancelled);
}

OFXIC_TEST(endpoint_request_control_overrides_timeout) {
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest & request) {
		captured = request;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"data":[]})";
		return response;
	});
	ofxIC::RequestControl control;
	control.timeoutSeconds = 3;
	OFXIC_REQUIRE(endpoint.inspect(control));
	OFXIC_REQUIRE(captured.timeoutSeconds == 3);
}

OFXIC_TEST(endpoint_rejects_negative_request_timeout_before_transport) {
	int calls = 0;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest &) {
		++calls;
		return ofxIC::HttpResponse{};
	});
	ofxIC::RequestControl control;
	control.timeoutSeconds = -1;
	const auto status = endpoint.inspect(control);
	OFXIC_REQUIRE(!status);
	OFXIC_REQUIRE(calls == 0);
	OFXIC_REQUIRE(status.failure == ofxIC::RequestFailure::InvalidResponse);
}

OFXIC_TEST(endpoint_rejects_oversized_model_response) {
	ofxIC::Endpoint endpoint("http://example.test", [](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body.assign(8U * 1024U * 1024U + 1U, 'x');
		return response;
	});

	const auto status = endpoint.inspect();
	OFXIC_REQUIRE(!status);
	OFXIC_REQUIRE(status.httpStatus == 0);
	OFXIC_REQUIRE(status.error.find("byte limit") != std::string::npos);
	OFXIC_REQUIRE(status.failure == ofxIC::RequestFailure::InvalidResponse);
}

OFXIC_TEST(endpoint_builds_chat_completions_body) {
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("http://localhost:8001", [&](const ofxIC::HttpRequest & httpRequest) {
		captured = httpRequest;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = "{\"choices\":[{\"message\":{\"content\":\"ok\"}}]}";
		return response;
	});
	ofxIC::ChatRequest request;
	request.systemPrompt = "Be brief";
	request.messages.push_back({ ofxIC::ChatRole::User, "Hello \"world\"" });
	request.options.model = "local/qwen";
	request.options.maxTokens = 42;
	request.options.temperature = 0.25f;
	request.options.seed = 7;
	request.options.stopSequences = { "</s>" };

	OFXIC_REQUIRE(endpoint.chat(request));
	const std::string & body = captured.body;
	OFXIC_REQUIRE(captured.method == ofxIC::HttpMethod::Post);
	OFXIC_REQUIRE(captured.url == "http://localhost:8001/v1/chat/completions");
	OFXIC_REQUIRE(body.find("\"model\":\"local/qwen\"") != std::string::npos);
	OFXIC_REQUIRE(body.find("\"role\":\"system\"") != std::string::npos);
	OFXIC_REQUIRE(body.find("Hello \\\"world\\\"") != std::string::npos);
	OFXIC_REQUIRE(body.find("\"max_tokens\":42") != std::string::npos);
	OFXIC_REQUIRE(body.find("\"seed\":7") != std::string::npos);
	OFXIC_REQUIRE(body.find("\"stop\":[\"</s>\"]") != std::string::npos);
}

OFXIC_TEST(endpoint_adds_bearer_auth_without_exposing_it_in_the_body) {
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("https://router.huggingface.co/v1", [&](const ofxIC::HttpRequest & request) {
		captured = request;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = "{\"choices\":[{\"message\":{\"content\":\"ok\"}}]}";
		return response;
	});
	endpoint.setBearerToken("hf_test_token");
	ofxIC::ChatRequest request;
	request.messages.push_back({ ofxIC::ChatRole::User, "hello" });
	request.options.model = "org/model:provider";

	OFXIC_REQUIRE(endpoint.chat(request));
	OFXIC_REQUIRE(endpoint.hasBearerToken());
	OFXIC_REQUIRE(captured.url == "https://router.huggingface.co/v1/chat/completions");
	OFXIC_REQUIRE(captured.headers.size() == 1);
	OFXIC_REQUIRE(captured.headers[0].first == "Authorization");
	OFXIC_REQUIRE(captured.headers[0].second == "Bearer hf_test_token");
	OFXIC_REQUIRE(captured.body.find("hf_test_token") == std::string::npos);
}

OFXIC_TEST(endpoint_extracts_chat_response_text) {
	std::string responseBody =
		"{\"choices\":[{\"message\":{\"content\":\"Hello\\nthere\"}}]}";
	ofxIC::Endpoint endpoint("http://localhost:8001", [&](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = responseBody;
		return response;
	});
	ofxIC::ChatRequest request;
	request.messages.push_back({ ofxIC::ChatRole::User, "Hello" });

	OFXIC_REQUIRE(endpoint.chat(request).text == "Hello\nthere");
	responseBody = "{\"choices\":[{\"text\":\"completion\"}]}";
	OFXIC_REQUIRE(endpoint.chat(request).text == "completion");
}

OFXIC_TEST(endpoint_extracts_openai_tool_calls) {
	ofxIC::Endpoint endpoint("http://localhost:8001", [](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"choices":[{"message":{"tool_calls":[{"function":{"arguments":"{\"query\":\"Grüße\"}","name":"search_documents"},"type":"function","id":"call-7"}]}}]})";
		return response;
	});
	ofxIC::ChatRequest request;
	request.messages.push_back({ ofxIC::ChatRole::User, "Search" });
	request.tools.push_back({ "search_documents", "Search documents", "{}" });

	const auto result = endpoint.chat(request);
	OFXIC_REQUIRE(result);
	const auto & calls = result.toolCalls;
	OFXIC_REQUIRE(calls.size() == 1);
	OFXIC_REQUIRE(calls[0].id == "call-7");
	OFXIC_REQUIRE(calls[0].name == "search_documents");
	OFXIC_REQUIRE(calls[0].argumentsJson.find("Grüße") != std::string::npos);
}

OFXIC_TEST(endpoint_reports_http_failures) {
	ofxIC::Endpoint endpoint("http://localhost:8001", [](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 503;
		response.error = "unavailable";
		return response;
	});
	ofxIC::ChatRequest request;
	request.messages.push_back({ ofxIC::ChatRole::User, "Hello" });

	const auto result = endpoint.chat(request);
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.httpStatus == 503);
	OFXIC_REQUIRE(result.error.find("HTTP 503") != std::string::npos);
}

OFXIC_TEST(endpoint_extracts_llama_text_serialized_requested_tool_call) {
	ofxIC::Endpoint endpoint("http://localhost:8001", [](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"choices":[{"message":{"content":"I will search. {{\"name\":\"search_documents\",\"arguments\":{\"query\":\"process boundary\"}}}"}}]})";
		return response;
	});
	ofxIC::ChatRequest request;
	request.messages.push_back({ ofxIC::ChatRole::User, "Search" });
	request.tools.push_back({ "search_documents", "Search documents", "{}" });

	const auto result = endpoint.chat(request);
	OFXIC_REQUIRE(result);
	OFXIC_REQUIRE(result.text.empty());
	OFXIC_REQUIRE(result.toolCalls.size() == 1);
	OFXIC_REQUIRE(result.toolCalls[0].name == "search_documents");
	OFXIC_REQUIRE(result.toolCalls[0].argumentsJson.find("process boundary") != std::string::npos);
}

OFXIC_TEST(endpoint_reports_bounded_provider_error_details) {
	ofxIC::Endpoint endpoint("https://provider.example", [](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 402;
		response.body = R"({"error":{"message":"Free quota exhausted; add credits"}})";
		return response;
	});
	ofxIC::ChatRequest request;
	request.messages.push_back({ ofxIC::ChatRole::User, "Hello" });

	const auto result = endpoint.chat(request);
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.httpStatus == 402);
	OFXIC_REQUIRE(result.error ==
		"chat endpoint returned HTTP 402: Free quota exhausted; add credits");
	OFXIC_REQUIRE(result.rawResponse.find("Free quota exhausted") != std::string::npos);
}

OFXIC_TEST(endpoint_rejects_streaming_tools_before_transport) {
	int calls = 0;
	ofxIC::Endpoint endpoint("http://localhost:8001", [&](const ofxIC::HttpRequest &) {
		++calls;
		return ofxIC::HttpResponse{};
	});
	ofxIC::ChatRequest request;
	request.messages.push_back({ ofxIC::ChatRole::User, "Hello" });
	request.tools.push_back({ "search_documents", "Search documents", "{}" });
	request.options.stream = true;
	const auto result = endpoint.chat(request);
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.error.find("not supported") != std::string::npos);
	OFXIC_REQUIRE(calls == 0);
}
