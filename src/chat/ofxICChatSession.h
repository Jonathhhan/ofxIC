#pragma once

#include "ofxICChatTypes.h"

#include <functional>
#include <string>
#include <vector>

namespace ofxIC {

class Endpoint;
class ToolLoop;

class ChatSession {
public:
	explicit ChatSession(Endpoint & endpoint);

	void setSystemPrompt(std::string systemPrompt);
	const std::string & getSystemPrompt() const;

	void setOptions(ChatOptions options);
	const ChatOptions & getOptions() const;

	const std::vector<ChatMessage> & getMessages() const;
	void clear();

	ChatResult send(
		const std::string & message,
		ChatChunkCallback onChunk = nullptr);

private:
	friend class ToolLoop;

	ChatResult complete(
		std::vector<ChatMessage> newMessages,
		const std::vector<ToolDefinition> & tools,
		ChatChunkCallback onChunk = nullptr);

	std::reference_wrapper<Endpoint> endpoint;
	std::string systemPrompt;
	ChatOptions options;
	std::vector<ChatMessage> messages;
};

} // namespace ofxIC
