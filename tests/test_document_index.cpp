#include "test_harness.h"
#include "../src/ofxIC.h"

#include <cstdio>
#include <fstream>
#include <string>

OFXIC_TEST(document_index_searches_only_explicitly_loaded_text) {
	ofxIC::DocumentIndex index;
	OFXIC_REQUIRE(index.addText(
		"architecture.md",
		"The process boundary keeps llama-server and CUDA outside the addon. "));
	OFXIC_REQUIRE(index.addText(
		"usage.md",
		"ChatSession keeps successful conversation history."));
	OFXIC_REQUIRE(!index.addText("architecture.md", "duplicate"));

	const auto hits = index.search("process boundary CUDA");
	OFXIC_REQUIRE(index.documentCount() == 2);
	OFXIC_REQUIRE(index.chunkCount() == 2);
	OFXIC_REQUIRE(hits.size() == 1);
	OFXIC_REQUIRE(hits[0].source == "architecture.md");
	OFXIC_REQUIRE(hits[0].citation == "[architecture.md#chunk-1]");
}

OFXIC_TEST(document_index_chunks_large_documents_and_limits_results) {
	ofxIC::DocumentIndex index;
	std::string text;
	for (int i = 0; i < 150; ++i) text += "Tools search grounded documents. ";
	OFXIC_REQUIRE(index.addText("long.txt", text));
	OFXIC_REQUIRE(index.chunkCount() > 1);
	OFXIC_REQUIRE(index.search("grounded documents", 1).size() == 1);
	OFXIC_REQUIRE(index.search("", 5).empty());
}

OFXIC_TEST(document_index_loads_a_file_with_an_explicit_source_identifier) {
	const std::string path = "ofxic-document-fixture.txt";
	{
		std::ofstream fixture(path, std::ios::binary | std::ios::trunc);
		fixture << "A deliberately loaded document keeps its private path out of citations.";
	}

	ofxIC::DocumentIndex index;
	OFXIC_REQUIRE(index.addFile(path, "fixture.txt"));
	OFXIC_REQUIRE(!index.addFile(path, "fixture.txt"));
	const auto hits = index.search("private path citations");
	OFXIC_REQUIRE(hits.size() == 1);
	OFXIC_REQUIRE(hits[0].source == "fixture.txt");
	OFXIC_REQUIRE(hits[0].citation == "[fixture.txt#chunk-1]");
	std::remove(path.c_str());
}
