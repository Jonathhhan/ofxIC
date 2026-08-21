#include "test_harness.h"
#include "../src/ofxIC.h"

OFXIC_TEST(tool_registry_is_an_explicit_allowlist) {
	ofxIC::DocumentIndex index;
	index.addText("guide.md", "Use a separate llama-server process boundary.");
	ofxIC::ToolRegistry tools;
	OFXIC_REQUIRE(tools.addDocumentSearch(index));
	OFXIC_REQUIRE(!tools.addDocumentSearch(index));

	ofxIC::ToolCall search{ "call-1", "search_documents", "{\"query\":\"process boundary\"}" };
	const auto found = tools.execute(search);
	OFXIC_REQUIRE(found);
	OFXIC_REQUIRE(found.content.find("[guide.md#chunk-1]") != std::string::npos);

	ofxIC::ToolCall unknown{ "call-2", "read_file", "{\"path\":\"secret\"}" };
	const auto rejected = tools.execute(unknown);
	OFXIC_REQUIRE(!rejected);
	OFXIC_REQUIRE(rejected.error.find("not allowlisted") != std::string::npos);
}

OFXIC_TEST(tool_loop_searches_then_returns_grounded_answer) {
	int requests = 0;
	ofxIC::Endpoint endpoint("https://example.test/v1", [&](const ofxIC::HttpRequest & request) {
		++requests;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		if (requests == 1) {
			OFXIC_REQUIRE(request.body.find("\"tools\"") != std::string::npos);
			response.body = R"({"choices":[{"message":{"role":"assistant","tool_calls":[{"id":"call-1","type":"function","function":{"name":"search_documents","arguments":"{\"query\":\"process boundary\"}"}}]}}]})";
		} else {
			OFXIC_REQUIRE(request.body.find("\"tool_call_id\":\"call-1\"") != std::string::npos);
			OFXIC_REQUIRE(request.body.find("guide.md#chunk-1") != std::string::npos);
			response.body = R"({"choices":[{"message":{"role":"assistant","content":"The runtime stays separate [guide.md#chunk-1]."}}]})";
		}
		return response;
	});

	ofxIC::ChatSession chat(endpoint);
	chat.setSystemPrompt("Use search_documents and cite its results.");
	ofxIC::DocumentIndex index;
	index.addText("guide.md", "The native runtime stays behind a process boundary.");
	ofxIC::ToolRegistry tools;
	tools.addDocumentSearch(index);
	ofxIC::ToolLoop loop(chat, tools);

	const auto result = loop.run("Why is the runtime separate?");
	OFXIC_REQUIRE(result);
	OFXIC_REQUIRE(result.modelRequests == 2);
	OFXIC_REQUIRE(result.steps.size() == 1);
	OFXIC_REQUIRE(result.text.find("[guide.md#chunk-1]") != std::string::npos);
	OFXIC_REQUIRE(chat.getMessages().size() == 4);
}
