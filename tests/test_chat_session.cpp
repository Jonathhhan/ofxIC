#include "test_harness.h"
#include "../src/ofxIC.h"

#include <stdexcept>

OFXIC_TEST(chat_session_keeps_successful_history) {
	int requestCount = 0;
	ofxIC::Endpoint endpoint("http://localhost:8001", [&](const ofxIC::HttpRequest & request) {
		++requestCount;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = requestCount == 1
			? "{\"choices\":[{\"message\":{\"content\":\"First answer\"}}]}"
			: "{\"choices\":[{\"message\":{\"content\":\"Second answer\"}}]}";
		OFXIC_REQUIRE(request.body.find("Be concise") != std::string::npos);
		return response;
	});

	ofxIC::ChatSession session(endpoint);
	session.setSystemPrompt("Be concise");
	const auto first = session.send("First question");
	const auto second = session.send("Second question");

	OFXIC_REQUIRE(first);
	OFXIC_REQUIRE(second);
	OFXIC_REQUIRE(session.getMessages().size() == 4);
	OFXIC_REQUIRE(session.getMessages()[1].content == "First answer");
	OFXIC_REQUIRE(session.getMessages()[3].content == "Second answer");
}

OFXIC_TEST(chat_session_rolls_back_failed_user_message) {
	ofxIC::Endpoint endpoint("http://localhost:8001", [](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 500;
		return response;
	});
	ofxIC::ChatSession session(endpoint);

	const auto result = session.send("This fails");
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(session.getMessages().empty());
}

OFXIC_TEST(chat_session_rejects_empty_messages_without_transport) {
	int calls = 0;
	ofxIC::Endpoint endpoint("http://localhost:8001", [&](const ofxIC::HttpRequest &) {
		++calls;
		return ofxIC::HttpResponse{};
	});
	ofxIC::ChatSession session(endpoint);

	const auto result = session.send("");
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.error == "message is empty");
	OFXIC_REQUIRE(calls == 0);
}

OFXIC_TEST(chat_session_forwards_cancellation_and_rolls_back_history) {
	bool receivedCancellation = false;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest & request) {
		receivedCancellation = request.shouldCancel && request.shouldCancel();
		ofxIC::HttpResponse response;
		response.started = true;
		response.cancelled = receivedCancellation;
		response.error = "request cancelled";
		return response;
	});
	ofxIC::ChatSession chat(endpoint);

	const auto result = chat.send("Stop this", nullptr, []() { return true; });

	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.cancelled);
	OFXIC_REQUIRE(receivedCancellation);
	OFXIC_REQUIRE(chat.getMessages().empty());
}

OFXIC_TEST(chat_session_preserves_successful_history_when_transport_throws) {
	bool fail = false;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest &) {
		if (fail) throw std::runtime_error("transport exception");
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"choices":[{"message":{"content":"Saved answer"}}]})";
		return response;
	});
	ofxIC::ChatSession session(endpoint);
	OFXIC_REQUIRE(session.send("Saved question"));
	fail = true;
	bool threw = false;
	try { session.send("Failed question"); }
	catch (const std::runtime_error &) { threw = true; }
	OFXIC_REQUIRE(threw);
	OFXIC_REQUIRE(session.getMessages().size() == 2);
	OFXIC_REQUIRE(session.getMessages().back().content == "Saved answer");
}
