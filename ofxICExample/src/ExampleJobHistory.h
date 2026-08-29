#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ofxICExample {

struct JobHistoryEntry {
	std::string timestamp;
	std::string task;
	std::string outcome;
	std::string detail;
	std::string outputPath;
};

class JobHistory {
public:
	explicit JobHistory(std::size_t maximumEntries = 100);

	bool load(const std::string & path);
	bool save(const std::string & path) const;
	bool clear(const std::string & path);
	void add(JobHistoryEntry entry);

	const std::vector<JobHistoryEntry> & entries() const;
	const std::string & status() const;

private:
	std::size_t maximumEntries;
	std::vector<JobHistoryEntry> history;
	std::string historyStatus;
};

} // namespace ofxICExample
