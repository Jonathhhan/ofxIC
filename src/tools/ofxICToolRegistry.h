#pragma once

#include "../chat/ofxICChatTypes.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ofxIC {

class DocumentIndex;

struct ToolExecutionResult {
	bool success = false;
	std::string content;
	std::string error;

	explicit operator bool() const { return success; }
};

using ToolHandler = std::function<ToolExecutionResult(const std::string & argumentsJson)>;

class ToolRegistry {
public:
	bool add(ToolDefinition definition, ToolHandler handler);
	bool addDocumentSearch(const DocumentIndex & index);
	bool contains(const std::string & name) const;
	std::vector<ToolDefinition> definitions() const;
	ToolExecutionResult execute(const ToolCall & call) const;

private:
	struct Entry {
		ToolDefinition definition;
		ToolHandler handler;
	};
	std::map<std::string, Entry> entries;
};

} // namespace ofxIC
