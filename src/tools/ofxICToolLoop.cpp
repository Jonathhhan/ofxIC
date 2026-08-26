#include "ofxICToolLoop.h"

#include "../chat/ofxICChatSession.h"

#include <utility>

namespace ofxIC {

ToolLoop::ToolLoop(ChatSession & chat, const ToolRegistry & tools)
	: chat(chat)
	, tools(tools) {
}

ToolLoopResult ToolLoop::run(
	const std::string & userMessage,
	std::size_t maxToolRounds,
	std::function<bool()> shouldCancel,
	ToolLoopProgressCallback onProgress) {
	RequestControl control;
	control.shouldCancel = std::move(shouldCancel);
	return run(userMessage, maxToolRounds, std::move(control), std::move(onProgress));
}

ToolLoopResult ToolLoop::run(
	const std::string & userMessage,
	std::size_t maxToolRounds,
	RequestControl control,
	ToolLoopProgressCallback onProgress) {
	ToolLoopResult loopResult;
	if (control.timeoutSeconds < 0) {
		loopResult.failure = RequestFailure::InvalidResponse;
		loopResult.error = "request timeout cannot be negative";
		return loopResult;
	}
	if (userMessage.empty()) {
		loopResult.failure = RequestFailure::InvalidResponse;
		loopResult.error = "message is empty";
		return loopResult;
	}
	ChatSession & session = chat.get();
	if (session.getOptions().stream) {
		loopResult.failure = RequestFailure::InvalidResponse;
		loopResult.error = "tool loop streaming is not supported yet";
		return loopResult;
	}
	const std::vector<ToolDefinition> definitions = tools.get().definitions();
	if (definitions.empty()) {
		loopResult.failure = RequestFailure::InvalidResponse;
		loopResult.error = "tool registry is empty";
		return loopResult;
	}
	const std::size_t checkpoint = session.messages.size();
	const auto cancelled = [&control]() {
		return control.shouldCancel && control.shouldCancel();
	};
	if (cancelled()) {
		loopResult.cancelled = true;
		loopResult.failure = RequestFailure::Cancelled;
		loopResult.error = "request cancelled";
		return loopResult;
	}
	ChatMessage user;
	user.role = ChatRole::User;
	user.content = userMessage;
	if (onProgress) {
		onProgress({ ToolLoopStage::RequestingModel, 1, {} });
	}
	ChatResult completion = session.complete({ user }, definitions, nullptr, control);
	++loopResult.modelRequests;

	for (std::size_t round = 0; completion && !completion.toolCalls.empty(); ++round) {
		if (cancelled()) {
			loopResult.cancelled = true;
			loopResult.failure = RequestFailure::Cancelled;
			loopResult.error = "request cancelled";
			session.messages.resize(checkpoint);
			return loopResult;
		}
		if (round >= maxToolRounds || completion.toolCalls.size() > 8) {
			loopResult.error = "tool call limit reached";
			session.messages.resize(checkpoint);
			return loopResult;
		}
		std::vector<ChatMessage> toolMessages;
		for (const ToolCall & call : completion.toolCalls) {
			if (onProgress) {
				onProgress({ ToolLoopStage::ExecutingTool, loopResult.modelRequests, call.name });
			}
			ToolExecutionResult execution = tools.get().execute(call);
			loopResult.steps.push_back({ call, execution });
			if (!execution) {
				loopResult.error = execution.error;
				session.messages.resize(checkpoint);
				return loopResult;
			}
			ChatMessage toolMessage;
			toolMessage.role = ChatRole::Tool;
			toolMessage.content = execution.content;
			toolMessage.toolCallId = call.id;
			toolMessages.push_back(std::move(toolMessage));
		}
		if (onProgress) {
			onProgress({ ToolLoopStage::RequestingModel, loopResult.modelRequests + 1, {} });
		}
		completion = session.complete(
			std::move(toolMessages), definitions, nullptr, control);
		++loopResult.modelRequests;
	}

	if (!completion) {
		loopResult.cancelled = completion.cancelled;
		loopResult.failure = completion.failure;
		loopResult.error = completion.error;
		session.messages.resize(checkpoint);
		return loopResult;
	}
	loopResult.success = true;
	loopResult.text = completion.text;
	return loopResult;
}

} // namespace ofxIC
