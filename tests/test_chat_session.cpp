#include "test_harness.h"
#include "../src/ofxIC.h"

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
