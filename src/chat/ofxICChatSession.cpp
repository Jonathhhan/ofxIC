#include "ofxICChatSession.h"

#include "../endpoint/ofxICEndpoint.h"

#include <utility>

namespace ofxIC {

ChatSession::ChatSession(Endpoint & endpoint)
	: endpoint(endpoint) {
}

void ChatSession::setSystemPrompt(std::string systemPrompt) {
	this->systemPrompt = std::move(systemPrompt);
}

const std::string & ChatSession::getSystemPrompt() const {
	return systemPrompt;
}

void ChatSession::setOptions(ChatOptions options) {
	this->options = std::move(options);
}

const ChatOptions & ChatSession::getOptions() const {
	return options;
}

const std::vector<ChatMessage> & ChatSession::getMessages() const {
	return messages;
}

void ChatSession::clear() {
	messages.clear();
}

ChatResult ChatSession::send(
	const std::string & message,
	ChatChunkCallback onChunk,
	std::function<bool()> shouldCancel) {
	RequestControl control;
	control.shouldCancel = std::move(shouldCancel);
	return send(message, std::move(onChunk), std::move(control));
}

ChatResult ChatSession::send(
	const std::string & message,
	ChatChunkCallback onChunk,
	RequestControl control) {
	if (message.empty()) {
		ChatResult result;
		result.failure = RequestFailure::Validation;
		result.error = "message is empty";
		return result;
	}

	ChatMessage userMessage;
	userMessage.role = ChatRole::User;
	userMessage.content = message;
	return complete({ userMessage }, {}, std::move(onChunk), std::move(control));
}

ChatResult ChatSession::complete(
	std::vector<ChatMessage> newMessages,
	const std::vector<ToolDefinition> & tools,
	ChatChunkCallback onChunk,
	RequestControl control) {
	ChatRequest request;
	request.systemPrompt = systemPrompt;
	request.messages = messages;
	request.messages.insert(request.messages.end(), newMessages.begin(), newMessages.end());
	request.tools = tools;
	request.options = options;

	ChatResult result = endpoint.get().chat(
		request, std::move(onChunk), std::move(control));
	if (result) {
		ChatMessage assistantMessage;
		assistantMessage.role = ChatRole::Assistant;
		assistantMessage.content = result.text;
		assistantMessage.toolCalls = result.toolCalls;
		request.messages.push_back(std::move(assistantMessage));
		// Commit only a complete exchange, including when transport/callbacks throw.
		messages.swap(request.messages);
	}
	return result;
}

} // namespace ofxIC
