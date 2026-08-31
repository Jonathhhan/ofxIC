#include "ofApp.h"
#include "imgui_stdlib.h"

#include <cstdlib>
#include <memory>

namespace {

std::string environmentValue(const char * name) {
#if defined(TARGET_WIN32)
	char * value = nullptr;
	size_t length = 0;
	if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
		return {};
	}
	const std::unique_ptr<char, decltype(&std::free)> owned(value, &std::free);
	return owned.get();
#else
	const char * value = std::getenv(name);
	return value ? value : "";
#endif
}

} // namespace

ofApp::~ofApp() {
	stopWorker();
}

void ofApp::setup() {
	ofSetWindowTitle("ofxIC minimal chat");
	ofSetBackgroundColor(24);
	ofLogToConsole();
	gui.setup(nullptr, true);

	const std::string configuredEndpoint = environmentValue("OFXIC_ENDPOINT_URL");
	endpoint.setBaseUrl(configuredEndpoint.empty()
		? "http://127.0.0.1:8080" : configuredEndpoint);
	const std::string token = environmentValue("OFXIC_API_KEY");
	if (!token.empty()) endpoint.setBearerToken(token);

	ofxIC::ChatOptions options;
	options.model = environmentValue("OFXIC_MODEL");
	options.maxTokens = 512;
	chat.setOptions(options);
	chat.setSystemPrompt("Answer clearly and concisely.");
	input = "Hello";
}

void ofApp::update() {
	std::lock_guard<std::mutex> lock(resultMutex);
	if (!resultReady) return;
	status = std::move(pendingStatus);
	answer = std::move(pendingAnswer);
	resultReady = false;
	busy = false;
}

void ofApp::draw() {
	gui.begin();
	ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_FirstUseEver);
	ImGui::Begin("ofxIC minimal chat");
	ImGui::TextWrapped("Endpoint: %s", endpoint.getBaseUrl().c_str());
	ImGui::SeparatorText("Message");
	ImGui::BeginDisabled(busy.load());
	ImGui::InputTextMultiline("##message", &input, ImVec2(-FLT_MIN, 90),
		ImGuiInputTextFlags_WordWrap);
	if (ImGui::Button("Send") && !input.empty()) sendMessage();
	ImGui::SameLine();
	if (ImGui::Button("Clear conversation")) {
		chat.clear();
		answer.clear();
		status = "Conversation cleared.";
	}
	ImGui::EndDisabled();
	if (busy.load() && ImGui::Button("Cancel request")) cancelRequested = true;
	ImGui::SeparatorText("Status");
	ImGui::TextWrapped("%s", status.c_str());
	ImGui::SeparatorText("Answer");
	ImGui::BeginChild("answer", ImVec2(0, 0), ImGuiChildFlags_Borders);
	ImGui::TextWrapped("%s", answer.c_str());
	ImGui::EndChild();
	ImGui::End();
	gui.end();
}

void ofApp::sendMessage() {
	if (input.empty() || busy.exchange(true)) return;
	if (worker.joinable()) worker.join();
	cancelRequested = false;
	const std::string message = input;
	input.clear();
	answer.clear();
	status = "Sending...";
	worker = std::thread([this, message] {
		ofxIC::RequestControl control;
		control.timeoutSeconds = 120;
		control.shouldCancel = [this] { return cancelRequested.load(); };
		const ofxIC::ChatResult result = chat.send(message, nullptr, control);
		{
			std::lock_guard<std::mutex> lock(resultMutex);
			pendingStatus = result ? "Completed." :
				(result.cancelled ? "Cancelled." : "Failed: " + result.error);
			pendingAnswer = result.text;
			resultReady = true;
		}
	});
}

void ofApp::stopWorker() {
	cancelRequested = true;
	if (worker.joinable()) worker.join();
}

void ofApp::exit() {
	stopWorker();
}
