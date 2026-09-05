#include "ofxICToolLoop.h"

#include "../chat/ofxICChatSession.h"

#include <utility>
#include <algorithm>

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
		loopResult.failure = RequestFailure::Validation;
		loopResult.error = "request timeout cannot be negative";
		return loopResult;
	}
	if (userMessage.empty()) {
		loopResult.failure = RequestFailure::Validation;
		loopResult.error = "message is empty";
		return loopResult;
	}
	ChatSession & session = chat.get();
	if (session.getOptions().stream) {
		loopResult.failure = RequestFailure::Validation;
		loopResult.error = "tool loop streaming is not supported yet";
		return loopResult;
	}
	const std::vector<ToolDefinition> definitions = tools.get().definitions();
	if (definitions.empty()) {
		loopResult.failure = RequestFailure::Validation;
		loopResult.error = "tool registry is empty";
		return loopResult;
	}
	const std::size_t checkpoint = session.messages.size();
	// A tool round is one history transaction. Also roll it back on exceptions
	// from application-provided handlers, progress callbacks or transports.
	struct HistoryTransaction {
		std::vector<ChatMessage> & messages;
		std::size_t checkpoint;
		bool committed = false;
		~HistoryTransaction() {
			if (!committed) messages.resize(checkpoint);
		}
	} transaction{ session.messages, checkpoint };
	const auto cancelled = [&control]() {
		return control.shouldCancel && control.shouldCancel();
	};
	const auto stopIfCancelled = [&]() {
		if (!cancelled()) return false;
		loopResult.cancelled = true;
		loopResult.failure = RequestFailure::Cancelled;
		loopResult.error = "request cancelled";
		return true;
	};
	if (stopIfCancelled()) return loopResult;
	ChatMessage user;
	user.role = ChatRole::User;
	user.content = userMessage;
	if (onProgress) {
		onProgress({ ToolLoopStage::RequestingModel, 1, {} });
	}
	if (stopIfCancelled()) return loopResult;
	ChatResult completion = session.complete({ user }, definitions, nullptr, control);
	++loopResult.modelRequests;

	for (std::size_t round = 0; completion && !completion.toolCalls.empty(); ++round) {
		if (stopIfCancelled()) return loopResult;
		if (round >= maxToolRounds || completion.toolCalls.size() > 8) {
			loopResult.failure = RequestFailure::Validation;
			loopResult.error = "tool call limit reached";
			return loopResult;
		}
		std::vector<ChatMessage> toolMessages;
		for (const ToolCall & call : completion.toolCalls) {
			if (stopIfCancelled()) return loopResult;
			if (onProgress) {
				onProgress({ ToolLoopStage::ExecutingTool, loopResult.modelRequests, call.name });
			}
			if (stopIfCancelled()) return loopResult;
			ToolExecutionResult execution = tools.get().execute(call);
			loopResult.steps.push_back({ call, execution });
			if (!execution) {
				loopResult.failure = RequestFailure::Validation;
				loopResult.error = execution.error;
				return loopResult;
			}
			ChatMessage toolMessage;
			toolMessage.role = ChatRole::Tool;
			toolMessage.content = execution.content;
			toolMessage.toolCallId = call.id;
			toolMessages.push_back(std::move(toolMessage));
		}
		if (stopIfCancelled()) return loopResult;
		if (onProgress) {
			onProgress({ ToolLoopStage::RequestingModel, loopResult.modelRequests + 1, {} });
		}
		if (stopIfCancelled()) return loopResult;
		completion = session.complete(
			std::move(toolMessages), definitions, nullptr, control);
		++loopResult.modelRequests;
	}

	std::vector<std::string> citations;
	for (const auto & step : loopResult.steps)
		for (const auto & citation : step.result.citations)
			if (!citation.empty() && std::find(citations.begin(), citations.end(), citation) == citations.end())
				citations.push_back(citation);
	const auto hasCitation = [&]() {
		return std::any_of(citations.begin(), citations.end(), [&](const std::string & citation) {
			return completion.text.find(citation) != std::string::npos;
		});
	};
	if (completion && !citations.empty() && !hasCitation()) {
		if (stopIfCancelled()) return loopResult;
		ChatMessage correction;
		correction.role = ChatRole::System;
		correction.content = "Rewrite the previous answer using only the retrieved evidence. "
			"Include at least one of these exact citation markers next to the claim it supports. "
			"Do not invent sources. Available citation markers:\n";
		for (const auto & citation : citations) correction.content += citation + "\n";
		if (onProgress) onProgress({ ToolLoopStage::RequestingModel, loopResult.modelRequests + 1, {} });
		if (stopIfCancelled()) return loopResult;
		completion = session.complete({ correction }, {}, nullptr, control);
		++loopResult.modelRequests;
	}
	if (!completion) {
		loopResult.cancelled = completion.cancelled;
		loopResult.failure = completion.failure;
		loopResult.error = completion.error;
		return loopResult;
	}
	if (stopIfCancelled()) return loopResult;
	if (!citations.empty() && (!hasCitation() || !completion.toolCalls.empty())) {
		loopResult.failure = RequestFailure::Validation;
		loopResult.error = "answer omitted a retrieved source citation after one correction request";
		return loopResult;
	}
	loopResult.success = true;
	loopResult.text = completion.text;
	transaction.committed = true;
	return loopResult;
}

} // namespace ofxIC
