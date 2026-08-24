#include "ofxICDocumentIndex.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace ofxIC {
namespace {

constexpr std::size_t chunkCharacters = 900;
constexpr std::size_t chunkOverlap = 120;

std::string lowerAscii(std::string value) {
	for (char & c : value) {
		const unsigned char byte = static_cast<unsigned char>(c);
		if (byte < 128U) c = static_cast<char>(std::tolower(byte));
	}
	return value;
}

std::vector<std::string> queryTerms(const std::string & query) {
	std::vector<std::string> terms;
	std::unordered_set<std::string> seen;
	std::string current;
	for (const unsigned char c : lowerAscii(query)) {
		if (c >= 128U || std::isalnum(c)) {
			current.push_back(static_cast<char>(c));
		} else if (!current.empty()) {
			if (seen.insert(current).second) terms.push_back(current);
			current.clear();
		}
	}
	if (!current.empty() && seen.insert(current).second) terms.push_back(current);
	return terms;
}

std::size_t occurrenceCount(const std::string & text, const std::string & term) {
	std::size_t count = 0;
	std::size_t position = 0;
	while ((position = text.find(term, position)) != std::string::npos) {
		++count;
		position += term.size();
	}
	return count;
}

std::string trimmedChunk(const std::string & text, std::size_t begin, std::size_t end) {
	while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
	while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
	return text.substr(begin, end - begin);
}

} // namespace

bool DocumentIndex::addText(const std::string & source, const std::string & text) {
	if (source.empty() || text.empty()) return false;
	if (std::find(sources.begin(), sources.end(), source) != sources.end()) return false;

	std::vector<Chunk> newChunks;
	std::size_t begin = 0;
	std::size_t index = 0;
	while (begin < text.size()) {
		std::size_t end = std::min(begin + chunkCharacters, text.size());
		if (end < text.size()) {
			const std::size_t minimumEnd = begin + chunkCharacters / 2;
			const std::size_t boundary = text.find_last_of(" \t\r\n", end);
			if (boundary != std::string::npos && boundary >= minimumEnd) end = boundary;
		}
		std::string chunkText = trimmedChunk(text, begin, end);
		if (!chunkText.empty()) newChunks.push_back({ source, std::move(chunkText), index++ });
		if (end == text.size()) break;
		const std::size_t next = end > chunkOverlap ? end - chunkOverlap : end;
		begin = next > begin ? next : end;
	}
	if (newChunks.empty()) return false;
	sources.push_back(source);
	chunks.insert(chunks.end(), newChunks.begin(), newChunks.end());
	return true;
}

bool DocumentIndex::addFile(const std::string & path) {
	return addFile(path, path);
}

bool DocumentIndex::addFile(const std::string & path, const std::string & source) {
	std::ifstream input(path, std::ios::binary);
	if (!input) return false;
	const std::string text(
		(std::istreambuf_iterator<char>(input)),
		std::istreambuf_iterator<char>());
	return addText(source, text);
}

std::vector<DocumentSearchHit> DocumentIndex::search(
	const std::string & query,
	std::size_t maxResults) const {
	std::vector<DocumentSearchHit> hits;
	if (maxResults == 0) return hits;
	const std::string normalizedQuery = lowerAscii(query);
	const std::vector<std::string> terms = queryTerms(query);
	if (terms.empty()) return hits;

	for (const Chunk & chunk : chunks) {
		const std::string normalizedText = lowerAscii(chunk.text);
		float score = 0.0f;
		std::size_t matchedTerms = 0;
		for (const std::string & term : terms) {
			const std::size_t occurrences = occurrenceCount(normalizedText, term);
			if (occurrences > 0) {
				++matchedTerms;
				score += 1.0f + static_cast<float>(std::min<std::size_t>(occurrences, 4)) * 0.25f;
			}
		}
		if (matchedTerms == 0) continue;
		score += static_cast<float>(matchedTerms) / static_cast<float>(terms.size());
		if (terms.size() > 1 && normalizedText.find(normalizedQuery) != std::string::npos) score += 2.0f;

		DocumentSearchHit hit;
		hit.source = chunk.source;
		hit.text = chunk.text;
		hit.score = score;
		hit.chunkIndex = chunk.index;
		hit.citation = "[" + chunk.source + "#chunk-" + std::to_string(chunk.index + 1) + "]";
		hits.push_back(std::move(hit));
	}

	std::stable_sort(hits.begin(), hits.end(), [](const auto & a, const auto & b) {
		if (a.score != b.score) return a.score > b.score;
		if (a.source != b.source) return a.source < b.source;
		return a.chunkIndex < b.chunkIndex;
	});
	if (hits.size() > maxResults) hits.resize(maxResults);
	return hits;
}

std::size_t DocumentIndex::documentCount() const {
	return sources.size();
}

std::size_t DocumentIndex::chunkCount() const {
	return chunks.size();
}

void DocumentIndex::clear() {
	sources.clear();
	chunks.clear();
}

} // namespace ofxIC
