#include "ExampleJobHistory.h"
#include "ExampleAtomicFile.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace ofxICExample {
namespace {

constexpr int historyVersion = 1;
constexpr std::size_t maximumFieldSize = 4096;

bool validField(const std::string & value) {
	return value.size() < maximumFieldSize &&
		value.find_first_of("\r\n") == std::string::npos &&
		value.find('\0') == std::string::npos;
}

std::string currentTimestamp() {
	const std::time_t now = std::chrono::system_clock::to_time_t(
		std::chrono::system_clock::now());
	std::tm local{};
#if defined(_WIN32)
	localtime_s(&local, &now);
#else
	localtime_r(&now, &local);
#endif
	std::ostringstream stream;
	stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
	return stream.str();
}

bool parseEntry(const std::string & line, JobHistoryEntry & entry) {
	std::istringstream stream(line);
	if (!(stream >> std::quoted(entry.timestamp) >> std::quoted(entry.task) >>
		std::quoted(entry.outcome) >> std::quoted(entry.detail) >>
		std::quoted(entry.outputPath))) return false;
	stream >> std::ws;
	return stream.eof() && validField(entry.timestamp) && validField(entry.task) &&
		validField(entry.outcome) && validField(entry.detail) && validField(entry.outputPath);
}

} // namespace

JobHistory::JobHistory(std::size_t requestedMaximumEntries)
	: maximumEntries(std::max<std::size_t>(1, requestedMaximumEntries)) {}

bool JobHistory::load(const std::string & path) {
	history.clear();
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		historyStatus = "No saved task history yet.";
		return true;
	}
	std::string line;
	if (!std::getline(input, line) || line != "version=1") {
		historyStatus = "Ignored invalid task history.";
		return false;
	}
	std::vector<JobHistoryEntry> loaded;
	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) continue;
		JobHistoryEntry entry;
		if (!parseEntry(line, entry)) {
			history.clear();
			historyStatus = "Ignored invalid task history.";
			return false;
		}
		loaded.push_back(std::move(entry));
	}
	if (!input.eof()) {
		historyStatus = "Could not read task history.";
		return false;
	}
	if (loaded.size() > maximumEntries)
		loaded.erase(loaded.begin(), loaded.end() - static_cast<std::ptrdiff_t>(maximumEntries));
	history = std::move(loaded);
	historyStatus = "Loaded " + std::to_string(history.size()) + " task history entries.";
	return true;
}

bool JobHistory::save(const std::string & path) const {
	if (path.empty()) return false;
	std::ostringstream output;
	output << "version=" << historyVersion << '\n';
	for (const auto & entry : history) {
		if (!validField(entry.timestamp) || !validField(entry.task) ||
			!validField(entry.outcome) || !validField(entry.detail) ||
			!validField(entry.outputPath)) return false;
		output << std::quoted(entry.timestamp) << ' ' << std::quoted(entry.task) << ' '
			<< std::quoted(entry.outcome) << ' ' << std::quoted(entry.detail) << ' '
			<< std::quoted(entry.outputPath) << '\n';
	}
	return output && writeTextFileAtomically(path, output.str());
}

bool JobHistory::clear(const std::string & path) {
	history.clear();
	if (path.empty()) return false;
	std::error_code error;
	const bool removed = std::filesystem::remove(std::filesystem::path(path), error);
	if (error) {
		historyStatus = "Could not remove task history.";
		return false;
	}
	historyStatus = removed ? "Task history cleared." : "Task history was already empty.";
	return true;
}

void JobHistory::add(JobHistoryEntry entry) {
	if (entry.timestamp.empty()) entry.timestamp = currentTimestamp();
	if (!validField(entry.timestamp) || !validField(entry.task) ||
		!validField(entry.outcome) || !validField(entry.detail) ||
		!validField(entry.outputPath)) {
		historyStatus = "Rejected oversized or multiline task history entry.";
		return;
	}
	history.push_back(std::move(entry));
	if (history.size() > maximumEntries)
		history.erase(history.begin(),
			history.begin() + static_cast<std::ptrdiff_t>(history.size() - maximumEntries));
	historyStatus = "Task history updated.";
}

const std::vector<JobHistoryEntry> & JobHistory::entries() const { return history; }
const std::string & JobHistory::status() const { return historyStatus; }

} // namespace ofxICExample
