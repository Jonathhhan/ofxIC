#pragma once

#include "ofxICToolRegistry.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace ofxIC {

class ChatSession;

struct ToolLoopStep {
	ToolCall call;
	ToolExecutionResult result;
};

struct ToolLoopResult {
	bool success = false;
	std::string text;
	std::string error;
	std::size_t modelRequests = 0;
	std::vector<ToolLoopStep> steps;

	explicit operator bool() const { return success; }
};

class ToolLoop {
public:
	ToolLoop(ChatSession & chat, const ToolRegistry & tools);
	ToolLoopResult run(const std::string & userMessage, std::size_t maxToolRounds = 4);

private:
	std::reference_wrapper<ChatSession> chat;
	std::reference_wrapper<const ToolRegistry> tools;
};

} // namespace ofxIC
