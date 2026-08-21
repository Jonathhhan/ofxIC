#include "ofApp.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <utility>

namespace {

struct EndpointProfile {
	const char * name;
	const char * url;
	const char * tokenEnvironment;
};

constexpr std::array<EndpointProfile, 5> endpointProfiles{{
	{ "llama-server", "http://127.0.0.1:8080", "OFXIC_API_KEY" },
	{ "LM Studio", "http://127.0.0.1:1234", "OFXIC_API_KEY" },
	{ "Hugging Face", "https://router.huggingface.co/v1", "HF_TOKEN" },
	{ "OpenAI", "https://api.openai.com/v1", "OPENAI_API_KEY" },
	{ "Custom", "", "OFXIC_API_KEY" },
}};

std::string environmentValue(const char * name) {
	const char * value = std::getenv(name);
	return value && *value ? value : "";
}

std::string configuredEndpointUrl() {
	const std::string value = environmentValue("OFXIC_ENDPOINT_URL");
	return value.empty() ? "http://127.0.0.1:8080" : value;
}

template <std::size_t Size>
void setTextBuffer(std::array<char, Size> & destination, const std::string & value) {
	static_assert(Size > 0, "text buffers need room for a terminator");
	const std::size_t length = std::min(value.size(), Size - 1);
	std::copy_n(value.data(), length, destination.data());
	destination[length] = '\0';
}

std::string normalizedProfileUrl(std::string url) {
	while (!url.empty() && url.back() == '/') url.pop_back();
	if (url.size() >= 3 && url.compare(url.size() - 3, 3, "/v1") == 0) {
		url.erase(url.size() - 3);
	}
	return url;
}

int profileForUrl(const std::string & url) {
	const std::string normalized = normalizedProfileUrl(url);
	for (std::size_t index = 0; index + 1 < endpointProfiles.size(); ++index) {
		if (normalizedProfileUrl(endpointProfiles[index].url) == normalized) {
			return static_cast<int>(index);
		}
	}
	return static_cast<int>(endpointProfiles.size() - 1);
}

void writeAutomationResult(const std::string & status, const std::string & output) {
	const std::string path = environmentValue("OFXIC_GUI_RESULT_PATH");
	if (path.empty()) return;
	std::ofstream result(path, std::ios::binary | std::ios::trunc);
	if (!result) {
		ofLogError("ofxIC") << "Could not write GUI result to " << path;
		return;
	}
	result << status << "\n" << output << "\n";
}

} // namespace

ofApp::ofApp()
	: endpoint(configuredEndpointUrl())
	, chat(endpoint)
	, toolLoop(chat, tools) {
	selectedProfile = profileForUrl(configuredEndpointUrl());
	setTextBuffer(endpointUrl, configuredEndpointUrl());
	setTextBuffer(modelId, environmentValue("OFXIC_MODEL"));
	endpoint.setBearerToken(configuredToken());
}

ofApp::~ofApp() {
	finishWorker();
}

void ofApp::setup() {
	ofSetWindowTitle("ofxIC Document Tool");
	ofSetBackgroundColor(20);
	gui.setup(nullptr, true);
	chat.setSystemPrompt(
		"Use search_documents for questions about the addon. "
		"Ground answers only in returned text and include its citation values.");
	ofxIC::ChatOptions options;
	options.model = modelId.data();
	chat.setOptions(options);
	documents.addText(
		"architecture.md",
		"ofxIC keeps llama-server, ggml, CUDA, and model runtimes outside "
		"the addon behind an HTTP process boundary. The addon provides endpoint "
		"access, chat history, explicit document search, and allowlisted tools.");
	tools.addDocumentSearch(documents);
	status = "Ready. Inspect the endpoint, then send a message.";
}

void ofApp::update() {
	if (!finished.exchange(false)) return;
	finishWorker();
	busy = false;
	std::lock_guard<std::mutex> lock(resultMutex);
	output = std::move(pendingOutput);
	status = std::move(pendingStatus);
	availableModels = std::move(pendingModels);
	if (!pendingModelSelection.empty()) {
		setTextBuffer(modelId, pendingModelSelection);
		ofxIC::ChatOptions options = chat.getOptions();
		options.model = pendingModelSelection;
		chat.setOptions(options);
		pendingModelSelection.clear();
	}
	writeAutomationResult(status, output);
}

void ofApp::draw() {
	bool applyRequested = false;
	bool inspectRequested = false;
	bool sendRequested = false;
	bool clearRequested = false;

	gui.begin();
	ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(928, 190), ImGuiCond_FirstUseEver);
	ImGui::Begin("Connection");
	ImGui::BeginDisabled(busy);
	if (ImGui::BeginCombo("Endpoint", endpointProfiles[selectedProfile].name)) {
		for (std::size_t index = 0; index < endpointProfiles.size(); ++index) {
			const bool selected = selectedProfile == static_cast<int>(index);
			if (ImGui::Selectable(endpointProfiles[index].name, selected)) {
				selectEndpointProfile(static_cast<int>(index));
			}
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	if (ImGui::InputText("Base URL", endpointUrl.data(), endpointUrl.size())) {
		configurationDirty = true;
	}
	if (ImGui::InputText("Model", modelId.data(), modelId.size())) {
		configurationDirty = true;
	}
	if (!availableModels.empty()) {
		ImGui::SameLine();
		if (ImGui::BeginCombo("Available", modelId[0] ? modelId.data() : "select model")) {
			for (const auto & model : availableModels) {
				ImGui::PushID(model.c_str());
				if (ImGui::Selectable(model.c_str(), model == modelId.data())) {
					setTextBuffer(modelId, model);
					configurationDirty = true;
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
	}
	applyRequested = ImGui::Button(configurationDirty ? "Apply *" : "Apply");
	ImGui::SameLine();
	inspectRequested = ImGui::Button("Inspect / models");
	ImGui::EndDisabled();
	ImGui::SameLine();
	const std::string token = configuredToken();
	const std::string tokenSource = configuredTokenSource();
	if (token.empty()) {
		ImGui::TextDisabled("Token: not loaded (%s)", tokenSource.c_str());
	} else {
		ImGui::Text("Token: loaded from %s", tokenSource.c_str());
	}
	ImGui::TextWrapped("%s", status.c_str());
	ImGui::End();

	ImGui::SetNextWindowPos(ImVec2(16, 218), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(928, 406), ImGuiCond_FirstUseEver);
	ImGui::Begin("Document tool chat");
	if (!lastMessage.empty()) {
		ImGui::TextDisabled("Last message: %s", lastMessage.c_str());
	}
	ImGui::TextUnformatted("Message");
	if (focusMessageInput && !busy) {
		ImGui::SetKeyboardFocusHere();
		focusMessageInput = false;
	}
	ImGui::BeginDisabled(busy);
	sendRequested = ImGui::InputTextMultiline(
		"##message",
		input.data(),
		input.size(),
		ImVec2(-1, 76),
		ImGuiInputTextFlags_EnterReturnsTrue);
	if (ImGui::Button("Send")) sendRequested = true;
	ImGui::SameLine();
	clearRequested = ImGui::Button("Clear conversation");
	ImGui::EndDisabled();
	if (busy) {
		ImGui::SameLine();
		ImGui::TextDisabled("Waiting for model...");
	}
	ImGui::SeparatorText("Response");
	ImGui::BeginChild("response", ImVec2(0, 0), true);
	ImGui::TextWrapped("%s", output.empty() ? "No response yet." : output.c_str());
	ImGui::EndChild();
	ImGui::End();
	gui.end();

	if (applyRequested) applyConfiguration();
	if (inspectRequested) {
		if (configurationDirty) applyConfiguration();
		inspectEndpoint();
	}
	if (clearRequested && !busy) {
		chat.clear();
		lastMessage.clear();
		output.clear();
		status = "Conversation cleared";
		focusMessageInput = true;
	}
	if (sendRequested) {
		if (configurationDirty) applyConfiguration();
		sendMessage();
	}
}

void ofApp::keyPressed(int key) {
	if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard) return;
	if (key == OF_KEY_F1) {
		if (configurationDirty) applyConfiguration();
		inspectEndpoint();
		return;
	}
	if (key == OF_KEY_F2) {
		if (!busy) {
			chat.clear();
			lastMessage.clear();
			output.clear();
			status = "Conversation cleared";
			focusMessageInput = true;
		}
		return;
	}
	if (key == OF_KEY_BACKSPACE) {
		if (input[0] && !busy) input[std::strlen(input.data()) - 1] = '\0';
		return;
	}
	if (key == OF_KEY_RETURN) {
		if (configurationDirty) applyConfiguration();
		sendMessage();
		return;
	}
	if (key >= 32 && key <= 126 && !busy) {
		const std::size_t length = std::strlen(input.data());
		if (length + 1 < input.size()) {
			input[length] = static_cast<char>(key);
			input[length + 1] = '\0';
		}
	}
}

void ofApp::exit() {
	finishWorker();
}

void ofApp::applyConfiguration() {
	if (busy) return;
	endpoint.setBaseUrl(endpointUrl.data());
	endpoint.setBearerToken(configuredToken());
	ofxIC::ChatOptions options = chat.getOptions();
	options.model = modelId.data();
	chat.setOptions(options);
	chat.clear();
	availableModels.clear();
	lastMessage.clear();
	output.clear();
	configurationDirty = false;
	status = "Applied " + std::string(endpointProfiles[selectedProfile].name) +
		" at " + endpoint.getBaseUrl();
}

void ofApp::selectEndpointProfile(int profileIndex) {
	if (profileIndex < 0 || profileIndex >= static_cast<int>(endpointProfiles.size())) return;
	selectedProfile = profileIndex;
	const EndpointProfile & profile = endpointProfiles[selectedProfile];
	if (*profile.url) {
		setTextBuffer(endpointUrl, profile.url);
		setTextBuffer(modelId, "");
	}
	configurationDirty = true;
	applyConfiguration();
}

std::string ofApp::configuredToken() const {
	const std::string generic = environmentValue("OFXIC_API_KEY");
	if (!generic.empty()) return generic;
	return environmentValue(endpointProfiles[selectedProfile].tokenEnvironment);
}

std::string ofApp::configuredTokenSource() const {
	if (!environmentValue("OFXIC_API_KEY").empty()) return "OFXIC_API_KEY";
	return endpointProfiles[selectedProfile].tokenEnvironment;
}

void ofApp::inspectEndpoint() {
	if (busy.exchange(true)) return;
	status = "Inspecting endpoint...";
	const std::string currentOutput = output;
	const std::string currentModel = chat.getOptions().model;
	worker = std::thread([this, currentOutput, currentModel]() {
		const auto inspection = endpoint.inspect();
		std::lock_guard<std::mutex> lock(resultMutex);
		pendingModels = inspection.models;
		pendingModelSelection.clear();
		if (!inspection) {
			pendingStatus = "Inspection failed: " + inspection.error;
		} else if (!currentModel.empty()) {
			pendingStatus = "Endpoint ready; configured model: " + currentModel;
		} else if (inspection.models.empty()) {
			pendingStatus = "Endpoint reachable; enter a model ID";
		} else {
			pendingModelSelection = inspection.models.front();
			pendingStatus = "Endpoint ready; model: " + pendingModelSelection;
		}
		pendingOutput = currentOutput;
		finished = true;
	});
}

void ofApp::sendMessage() {
	if (!input[0] || busy.exchange(true)) return;
	const std::string message(input.data());
	input[0] = '\0';
	lastMessage = message;
	status = "Waiting for model...";
	focusMessageInput = true;
	const std::vector<std::string> currentModels = availableModels;
	worker = std::thread([this, message, currentModels]() {
		const auto result = toolLoop.run(message);
		std::lock_guard<std::mutex> lock(resultMutex);
		pendingOutput = result.text;
		pendingStatus = result
			? "Completed with " + ofToString(result.modelRequests) + " model request(s)"
			: "Request failed: " + result.error;
		pendingModels = currentModels;
		finished = true;
	});
}

void ofApp::finishWorker() {
	if (worker.joinable()) worker.join();
}
