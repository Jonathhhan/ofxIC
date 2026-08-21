#include "test_harness.h"
#include "../src/ofxIC.h"

OFXIC_TEST(public_umbrella_header_exposes_api) {
	ofxIC::Endpoint endpoint("http://localhost:8001/v1");
	ofxIC::ChatSession chat(endpoint);
	ofxIC::DocumentIndex documents;
	ofxIC::ToolRegistry tools;
	tools.addDocumentSearch(documents);
	ofxIC::ToolLoop loop(chat, tools);
	OFXIC_REQUIRE(endpoint.getBaseUrl() == "http://localhost:8001");
	OFXIC_REQUIRE(chat.getMessages().empty());
	OFXIC_REQUIRE(tools.contains("search_documents"));
}
