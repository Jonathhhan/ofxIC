#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ofxIC {

struct DocumentSearchHit {
	std::string citation;
	std::string source;
	std::string text;
	float score = 0.0f;
	std::size_t chunkIndex = 0;
};

class DocumentIndex {
public:
	bool addText(const std::string & source, const std::string & text);
	bool addFile(const std::string & path);
	std::vector<DocumentSearchHit> search(
		const std::string & query,
		std::size_t maxResults = 5) const;

	std::size_t documentCount() const;
	std::size_t chunkCount() const;
	void clear();

private:
	struct Chunk {
		std::string source;
		std::string text;
		std::size_t index = 0;
	};

	std::vector<std::string> sources;
	std::vector<Chunk> chunks;
};

} // namespace ofxIC
