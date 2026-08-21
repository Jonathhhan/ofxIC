#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ofxIC {

struct ToolCall {
	std::string id;
	std::string name;
	std::string argumentsJson;
};

struct ToolDefinition {
	std::string name;
	std::string description;
	std::string parametersJson = "{\"type\":\"object\"}";
};

enum class ChatRole {
	System,
	User,
	Assistant,
	Tool
};

struct ChatMessage {
	ChatMessage() = default;
	ChatMessage(ChatRole role, std::string content)
		: role(role)
		, content(std::move(content)) {
	}

	ChatRole role = ChatRole::User;
	std::string content;
	std::string toolCallId;
	std::vector<ToolCall> toolCalls;
};

struct ChatOptions {
	std::string model;
	int maxTokens = 512;
	float temperature = 0.7f;
	float topP = 0.95f;
	int seed = -1;
	bool stream = false;
	std::vector<std::string> stopSequences;
};

struct ChatRequest {
	std::string systemPrompt;
	std::vector<ChatMessage> messages;
	std::vector<ToolDefinition> tools;
	ChatOptions options;
};

struct ChatResult {
	bool success = false;
	bool cancelled = false;
	int httpStatus = 0;
	float elapsedMs = 0.0f;
	std::string text;
	std::vector<ToolCall> toolCalls;
	std::string error;
	std::string rawResponse;

	explicit operator bool() const {
		return success;
	}
};

using ChatChunkCallback = std::function<bool(const std::string &)>;

} // namespace ofxIC
