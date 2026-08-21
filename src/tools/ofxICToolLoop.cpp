#include "ofxICToolLoop.h"

#include "../chat/ofxICChatSession.h"

namespace ofxIC {

ToolLoop::ToolLoop(ChatSession & chat, const ToolRegistry & tools)
	: chat(chat)
	, tools(tools) {
}

ToolLoopResult ToolLoop::run(const std::string & userMessage, std::size_t maxToolRounds) {
	ToolLoopResult loopResult;
	if (userMessage.empty()) {
		loopResult.error = "message is empty";
		return loopResult;
	}
	ChatSession & session = chat.get();
	if (session.getOptions().stream) {
		loopResult.error = "tool loop streaming is not supported yet";
		return loopResult;
	}
	const std::vector<ToolDefinition> definitions = tools.get().definitions();
	if (definitions.empty()) {
		loopResult.error = "tool registry is empty";
		return loopResult;
	}
	const std::size_t checkpoint = session.messages.size();
	ChatMessage user;
	user.role = ChatRole::User;
	user.content = userMessage;
	ChatResult completion = session.complete({ user }, definitions);
	++loopResult.modelRequests;

	for (std::size_t round = 0; completion && !completion.toolCalls.empty(); ++round) {
		if (round >= maxToolRounds || completion.toolCalls.size() > 8) {
			loopResult.error = "tool call limit reached";
			session.messages.resize(checkpoint);
			return loopResult;
		}
		std::vector<ChatMessage> toolMessages;
		for (const ToolCall & call : completion.toolCalls) {
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
		completion = session.complete(std::move(toolMessages), definitions);
		++loopResult.modelRequests;
	}

	if (!completion) {
		loopResult.error = completion.error;
		session.messages.resize(checkpoint);
		return loopResult;
	}
	loopResult.success = true;
	loopResult.text = completion.text;
	return loopResult;
}

} // namespace ofxIC
