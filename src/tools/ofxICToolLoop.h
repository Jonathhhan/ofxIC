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

enum class ToolLoopStage {
	RequestingModel,
	ExecutingTool
};

struct ToolLoopProgress {
	ToolLoopStage stage = ToolLoopStage::RequestingModel;
	std::size_t modelRequest = 0;
	std::string toolName;
};

using ToolLoopProgressCallback = std::function<void(const ToolLoopProgress &)>;

struct ToolLoopResult {
	bool success = false;
	bool cancelled = false;
	RequestFailure failure = RequestFailure::None;
	std::string text;
	std::string error;
	std::size_t modelRequests = 0;
	std::vector<ToolLoopStep> steps;

	explicit operator bool() const { return success; }
};

class ToolLoop {
public:
	ToolLoop(ChatSession & chat, const ToolRegistry & tools);
	// Cancellation is checked between handlers, not inside a running handler.
	// An incomplete run rolls back chat history, not side effects of executed
	// tools. Callback exceptions propagate; callbacks must not mutate the session.
	ToolLoopResult run(
		const std::string & userMessage,
		std::size_t maxToolRounds = 4,
		RequestControl control = {},
		ToolLoopProgressCallback onProgress = nullptr);
	ToolLoopResult run(const std::string & userMessage, std::size_t maxToolRounds,
		std::function<bool()> shouldCancel, ToolLoopProgressCallback onProgress = nullptr);

private:
	std::reference_wrapper<ChatSession> chat;
	std::reference_wrapper<const ToolRegistry> tools;
};

} // namespace ofxIC
