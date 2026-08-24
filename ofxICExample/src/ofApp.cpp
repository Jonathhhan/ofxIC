#include "ofApp.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
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

struct MediaBackendProfile {
	const char * name;
	const char * url;
	const char * tokenEnvironment;
	bool supportsImage;
	bool supportsVideo;
	const char * capabilityNote;
};

constexpr std::array<MediaBackendProfile, 3> mediaBackends{{
	{ "OpenAI images", "https://api.openai.com/v1", "OPENAI_API_KEY",
		true, false, "Image generation only; no OpenAI video adapter." },
	{ "Hugging Face / fal-ai", "https://router.huggingface.co", "HF_TOKEN",
		true, true, "Hosted image and queued video; provider credit may be required." },
	{ "stable-diffusion.cpp", "http://127.0.0.1:8080", "OFXIC_API_KEY",
		true, true, "External native image and video jobs." },
}};

bool supportsMediaKind(int backend, int kind) {
	if (backend < 0 || backend >= static_cast<int>(mediaBackends.size())) return false;
	return kind == 0
		? mediaBackends[backend].supportsImage
		: kind == 1 && mediaBackends[backend].supportsVideo;
}

std::string unsupportedMediaMessage(int backend, int kind) {
	if (backend < 0 || backend >= static_cast<int>(mediaBackends.size())) {
		return "Unknown media backend";
	}
	return std::string(mediaBackends[backend].name) + " does not support " +
		(kind == 1 ? "video" : "image") + " in ofxIC";
}

std::string environmentValue(const char * name) {
#if defined(_WIN32)
	char * value = nullptr;
	std::size_t length = 0;
	if (_dupenv_s(&value, &length, name) != 0 || !value) return {};
	std::string result(value);
	std::free(value);
	return result;
#else
	const char * value = std::getenv(name);
	return value && *value ? value : "";
#endif
}

std::string tokenSetupHint(const std::string & variable) {
#if defined(_WIN32)
	return "PowerShell before launch: $env:" + variable + " = \"your_token\"";
#else
	return "Shell before launch: export " + variable + "=your_token";
#endif
}

std::map<std::string, std::string> settingsEnvironment() {
	std::map<std::string, std::string> values;
	constexpr std::array<const char *, 11> names{{
		"OFXIC_ENDPOINT_URL",
		"OFXIC_MODEL",
		"OFXIC_MEDIA_BACKEND",
		"OFXIC_MEDIA_ENDPOINT_URL",
		"OFXIC_MEDIA_IMAGE_MODEL",
		"OFXIC_MEDIA_VIDEO_MODEL",
		"OFXIC_MEDIA_KIND",
		"OFXIC_MEDIA_WIDTH",
		"OFXIC_MEDIA_HEIGHT",
		"OFXIC_MEDIA_FRAMES",
		"OFXIC_MEDIA_FPS",
	}};
	for (const char * name : names) {
		const std::string value = environmentValue(name);
		if (!value.empty()) values.emplace(name, value);
	}
	return values;
}

std::string configuredSettingsPath() {
	const std::string configured = environmentValue("OFXIC_SETTINGS_PATH");
	if (!configured.empty()) return configured;
	return ofFilePath::join(ofFilePath::getUserHomeDir(), ".ofxICExample.settings");
}

std::string documentSourceName(const std::string & path) {
	const std::size_t separator = path.find_last_of("/\\");
	return separator == std::string::npos ? path : path.substr(separator + 1);
}

bool supportedDocumentPath(const std::string & path) {
	const std::string source = documentSourceName(path);
	const std::size_t dot = source.find_last_of('.');
	if (dot == std::string::npos) return false;
	std::string extension = source.substr(dot);
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return extension == ".md" || extension == ".txt";
}

std::string decodeBase64(const std::string & encoded) {
	static const std::string alphabet =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string decoded;
	unsigned int value = 0;
	int bits = -8;
	for (const unsigned char character : encoded) {
		if (character == '=') break;
		const std::size_t position = alphabet.find(static_cast<char>(character));
		if (position == std::string::npos) {
			if (std::isspace(character)) continue;
			return {};
		}
		value = (value << 6U) + static_cast<unsigned int>(position);
		bits += 6;
		if (bits >= 0) {
			decoded.push_back(static_cast<char>((value >> bits) & 0xff));
			bits -= 8;
		}
	}
	return decoded;
}

const char * mediaJobStateLabel(ofxIC::MediaJobState state) {
	switch (state) {
	case ofxIC::MediaJobState::Queued: return "queued";
	case ofxIC::MediaJobState::Generating: return "generating";
	case ofxIC::MediaJobState::Completed: return "completed";
	case ofxIC::MediaJobState::Failed: return "failed";
	case ofxIC::MediaJobState::Cancelled: return "cancelled";
	default: return "unknown";
	}
}

template <std::size_t Size>
void setTextBuffer(std::array<char, Size> & destination, const std::string & value) {
	static_assert(Size > 0, "text buffers need room for a terminator");
	const std::size_t length = std::min(value.size(), Size - 1);
	std::copy_n(value.data(), length, destination.data());
	destination[length] = '\0';
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

void writeMediaAutomationResult(const std::string & status, const std::string & output) {
	const std::string path = environmentValue("OFXIC_MEDIA_RESULT_PATH");
	if (path.empty()) return;
	std::ofstream result(path, std::ios::binary | std::ios::trunc);
	if (!result) {
		ofLogError("ofxIC") << "Could not write media GUI result to " << path;
		return;
	}
	result << status << "\n" << output << "\n";
}

void writeDocumentAutomationResult(
	const std::string & status,
	const std::vector<std::string> & sources) {
	const std::string path = environmentValue("OFXIC_DOCUMENT_RESULT_PATH");
	if (path.empty()) return;
	std::ofstream result(path, std::ios::binary | std::ios::trunc);
	if (!result) {
		ofLogError("ofxIC") << "Could not write document GUI result to " << path;
		return;
	}
	result << status << "\n";
	for (const std::string & source : sources) result << source << "\n";
}

} // namespace

ofApp::ofApp()
	: endpoint("http://127.0.0.1:8080")
	, mediaEndpoint("http://127.0.0.1:8080")
	, chat(endpoint)
	, media(mediaEndpoint)
	, toolLoop(chat, tools) {
	settingsPath = configuredSettingsPath();
	ofxICExample::ExampleSettings settings;
	const auto loadStatus = ofxICExample::loadSettings(settingsPath, settings);
	if (loadStatus == ofxICExample::SettingsLoadStatus::Loaded) {
		settingsStatus = "Loaded saved non-secret settings.";
	} else if (loadStatus == ofxICExample::SettingsLoadStatus::Invalid) {
		settingsStatus = "Ignored corrupt settings; using safe defaults.";
	} else {
		settingsStatus = "Using built-in defaults; no saved settings yet.";
	}
	ofxICExample::applyEnvironmentOverrides(settings, settingsEnvironment());
	applySettingsToUi(settings);
	if (ofxICExample::credentialStoreAvailable()) {
		for (const char * variable : { "HF_TOKEN", "OPENAI_API_KEY", "OFXIC_API_KEY" }) {
			std::string token;
			std::string error;
			if (ofxICExample::loadCredential(variable, token, error) && !token.empty()) {
				storedTokens[variable] = std::move(token);
			} else if (!error.empty()) {
				credentialStatus = error;
			}
		}
	}
	endpoint.setBaseUrl(endpointUrl.data());
	mediaEndpoint.setBaseUrl(mediaEndpointUrl.data());
	endpoint.setBearerToken(configuredToken());
	mediaEndpoint.setBearerToken(configuredMediaToken());
}

ofApp::~ofApp() {
	cancellationRequested = true;
	finishWorker();
	finishMediaWorker();
	tokenInput.fill('\0');
	mediaTokenInput.fill('\0');
	for (auto & entry : storedTokens) {
		std::fill(entry.second.begin(), entry.second.end(), '\0');
	}
	storedTokens.clear();
}

void ofApp::setup() {
	ofDisableArbTex();
	ofSetWindowTitle("ofxIC Endpoint Workbench");
	ofSetBackgroundColor(20);
	gui.setup(nullptr, true);
	chat.setSystemPrompt(
		"Use search_documents for questions that may be answered by loaded sources. "
		"Ground answers only in returned text and include its citation values.");
	ofxIC::ChatOptions options;
	options.model = modelId.data();
	chat.setOptions(options);
	if (documents.addText(
		"architecture.md",
		"ofxIC keeps llama-server, ggml, CUDA, and model runtimes outside "
		"the addon behind an HTTP process boundary. The addon provides endpoint "
		"access, chat history, explicit document search, and allowlisted tools.")) {
		loadedDocumentSources.push_back("architecture.md");
	}
	tools.addDocumentSearch(documents);
	documentStatus = "Drop a .md or .txt file here, or choose one explicitly.";
	const std::string documentPath = environmentValue("OFXIC_DOCUMENT_PATH");
	if (!documentPath.empty()) loadDocument(documentPath);
	status = "Ready. Inspect the endpoint, then send a message.";
	if (environmentValue("OFXIC_INSPECT_AUTORUN") == "1") inspectEndpoint();
	setTextBuffer(mediaInput, "A small paper sculpture on a clean studio background");
	mediaStatus = "Choose OpenAI images, Hugging Face / fal-ai, or stable-diffusion.cpp jobs.";
	const std::string mediaAutorun = environmentValue("OFXIC_MEDIA_AUTORUN");
	if (mediaAutorun == "image" || mediaAutorun == "video") {
		selectedMediaKind = mediaAutorun == "video" ? 1 : 0;
		const std::string prompt = environmentValue("OFXIC_MEDIA_PROMPT");
		if (!prompt.empty()) setTextBuffer(mediaInput, prompt);
		if (supportsMediaKind(selectedMediaBackend, selectedMediaKind)) {
			generateMedia();
		} else {
			mediaStatus = unsupportedMediaMessage(selectedMediaBackend, selectedMediaKind);
			writeMediaAutomationResult(mediaStatus, "");
		}
	}
}

void ofApp::update() {
	if (busy && requestCanCancel) {
		std::lock_guard<std::mutex> lock(resultMutex);
		if (!pendingProgressStatus.empty()) status = pendingProgressStatus;
	}
	if (finished.exchange(false)) {
		finishWorker();
		busy = false;
		requestCanCancel = false;
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
	if (mediaFinished.exchange(false)) {
		finishMediaWorker();
		mediaBusy = false;
		std::string base64;
		std::string bytes;
		std::string format;
		bool isVideo = false;
		{
			std::lock_guard<std::mutex> lock(mediaResultMutex);
			mediaStatus = std::move(pendingMediaStatus);
			mediaOutput = std::move(pendingMediaOutput);
			currentMediaJob = std::move(pendingMediaJob);
			base64 = std::move(pendingMediaBase64);
			bytes = std::move(pendingMediaBytes);
			format = std::move(pendingMediaFormat);
			isVideo = pendingMediaIsVideo;
			pendingMediaIsVideo = false;
		}
		if (!base64.empty() || !bytes.empty()) {
			if (bytes.empty()) bytes = decodeBase64(base64);
			const std::string extension = format.empty() ? (isVideo ? "webm" : "png") : format;
			const std::string path = ofToDataPath("ofxIC-last-media." + extension, true);
			if (ofBufferToFile(path, ofBuffer(bytes.data(), bytes.size()))) {
				if (isVideo) {
					generatedImage.clear();
					generatedVideo.close();
					generatedVideo.load(path);
					generatedVideo.setLoopState(OF_LOOP_NORMAL);
					generatedVideo.play();
				} else {
					generatedVideo.close();
					generatedImage.load(path);
				}
				mediaOutput += "\nSaved: " + path;
			}
		}
		writeMediaAutomationResult(mediaStatus, mediaOutput);
	}
	generatedVideo.update();
}

void ofApp::draw() {
	bool applyRequested = false;
	bool inspectRequested = false;
	bool sendRequested = false;
	bool cancelRequested = false;
	bool saveTokenRequested = false;
	bool forgetTokenRequested = false;
	bool saveMediaTokenRequested = false;
	bool forgetMediaTokenRequested = false;
	bool clearRequested = false;
	bool loadDocumentRequested = false;
	bool generateMediaRequested = false;
	bool pollMediaRequested = false;
	bool saveSettingsRequested = false;
	bool resetSettingsRequested = false;

	gui.begin();
	ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(1168, 220), ImGuiCond_FirstUseEver);
	ImGui::Begin("Connection");
	ImGui::BeginDisabled(busy || mediaBusy);
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
	ImGui::SameLine();
	saveSettingsRequested = ImGui::Button("Save settings");
	ImGui::SameLine();
	resetSettingsRequested = ImGui::Button("Reset saved settings");
	ImGui::EndDisabled();
	ImGui::SameLine();
	const std::string token = configuredToken();
	const std::string tokenSource = configuredTokenSource();
	if (token.empty()) {
		ImGui::TextDisabled("Token: not loaded (%s)", tokenSource.c_str());
		ImGui::TextWrapped("If authentication is required, set it without storing it: %s",
			tokenSetupHint(tokenSource).c_str());
	} else {
		ImGui::Text("Token: loaded from %s", tokenSource.c_str());
	}
	if (ofxICExample::credentialStoreAvailable()) {
		ImGui::SetNextItemWidth(280);
		ImGui::InputText("Token##chat-token", tokenInput.data(), tokenInput.size(),
			ImGuiInputTextFlags_Password);
		ImGui::SameLine();
		ImGui::BeginDisabled(busy || mediaBusy || !tokenInput[0]);
		saveTokenRequested = ImGui::Button("Save securely##chat-token");
		ImGui::EndDisabled();
		ImGui::SameLine();
		const std::string preferredToken = endpointProfiles[selectedProfile].tokenEnvironment;
		ImGui::BeginDisabled(busy || mediaBusy || storedTokens.count(preferredToken) == 0);
		forgetTokenRequested = ImGui::Button("Forget saved token##chat-token");
		ImGui::EndDisabled();
	}
	if (!credentialStatus.empty()) ImGui::TextWrapped("%s", credentialStatus.c_str());
	ImGui::TextWrapped("%s", status.c_str());
	ImGui::TextDisabled("%s Tokens are never stored in settings.", settingsStatus.c_str());
	ImGui::End();

	ImGui::SetNextWindowPos(ImVec2(16, 248), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(568, 426), ImGuiCond_FirstUseEver);
	ImGui::Begin("Document tool chat");
	loadDocumentRequested = ImGui::Button("Load .md / .txt");
	ImGui::SameLine();
	ImGui::TextDisabled("%zu document(s), %zu chunk(s)",
		documents.documentCount(), documents.chunkCount());
	ImGui::TextWrapped("%s", documentStatus.c_str());
	if (ImGui::CollapsingHeader("Loaded sources", ImGuiTreeNodeFlags_DefaultOpen)) {
		for (const std::string & source : loadedDocumentSources) {
			ImGui::BulletText("%s", source.c_str());
		}
	}
	ImGui::Separator();
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
		if (requestCanCancel) {
			cancelRequested = ImGui::Button("Cancel request");
			ImGui::SameLine();
			ImGui::TextDisabled(cancellationRequested ? "Cancelling..." : "Waiting for endpoint...");
		} else {
			ImGui::TextDisabled("Waiting for endpoint...");
		}
	}
	ImGui::SeparatorText("Response");
	ImGui::BeginChild("response", ImVec2(0, 0), true);
	ImGui::TextWrapped("%s", output.empty() ? "No response yet." : output.c_str());
	ImGui::EndChild();
	ImGui::End();

	ImGui::SetNextWindowPos(ImVec2(600, 248), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(584, 426), ImGuiCond_FirstUseEver);
	ImGui::Begin("Image and video");
	const char * backendNames[] = { "OpenAI images", "Hugging Face / fal-ai", "stable-diffusion.cpp" };
	const char * mediaKinds[] = { "Image", "Video" };
	ImGui::BeginDisabled(busy || mediaBusy);
	int nextMediaBackend = selectedMediaBackend;
	if (ImGui::Combo("Media backend", &nextMediaBackend, backendNames, 3)) {
		selectMediaBackend(nextMediaBackend);
	}
	const MediaBackendProfile & mediaProfile = mediaBackends[selectedMediaBackend];
	ImGui::TextDisabled(
		"Capabilities: Image: %s | Video: %s",
		mediaProfile.supportsImage ? "yes" : "no",
		mediaProfile.supportsVideo ? "yes" : "no");
	ImGui::TextWrapped("%s", mediaProfile.capabilityNote);
	if (mediaProfile.supportsImage && mediaProfile.supportsVideo) {
		ImGui::Combo("Kind", &selectedMediaKind, mediaKinds, 2);
	} else {
		selectedMediaKind = mediaProfile.supportsVideo ? 1 : 0;
		ImGui::Text("Kind: %s", mediaKinds[selectedMediaKind]);
	}
	if (selectedMediaBackend != 1) {
		if (ImGui::InputText(
			"Media base URL", mediaEndpointUrl.data(), mediaEndpointUrl.size())) {
			mediaConfigurationDirty = true;
		}
	} else {
		ImGui::TextDisabled("fal-ai through Hugging Face routing");
	}
	if (selectedMediaBackend != 2) {
		auto & mediaModel = selectedMediaKind == 0 ? mediaImageModel : mediaVideoModel;
		ImGui::InputText("Media model", mediaModel.data(), mediaModel.size());
	}
	ImGui::InputTextMultiline(
		"##media-prompt",
		mediaInput.data(),
		mediaInput.size(),
		ImVec2(-1, 64));
	ImGui::InputInt("Width", &mediaWidth);
	ImGui::SameLine();
	ImGui::InputInt("Height", &mediaHeight);
	if (selectedMediaKind == 1) {
		ImGui::InputInt("Frames", &mediaFrames);
		ImGui::SameLine();
		ImGui::InputInt("FPS", &mediaFps);
	}
	const char * generateLabel = selectedMediaBackend == 1
		? (selectedMediaKind == 0 ? "Generate HF image" : "Submit HF video")
		: (selectedMediaBackend == 2
			? (selectedMediaKind == 0 ? "Submit image job" : "Submit video job")
			: "Generate OpenAI image");
	generateMediaRequested = ImGui::Button(generateLabel);
	if (!currentMediaJob.id.empty() && !currentMediaJob.terminal()) {
		ImGui::SameLine();
		pollMediaRequested = ImGui::Button("Poll job");
	}
	ImGui::EndDisabled();
	const std::string mediaToken = configuredMediaToken();
	ImGui::TextDisabled("Media token: %s (%s)",
		mediaToken.empty() ? "not loaded" : "loaded",
		configuredMediaTokenSource().c_str());
	if (mediaToken.empty()) {
		ImGui::TextWrapped("If authentication is required: %s",
			tokenSetupHint(mediaBackends[selectedMediaBackend].tokenEnvironment).c_str());
	}
	if (ofxICExample::credentialStoreAvailable()) {
		ImGui::SetNextItemWidth(280);
		ImGui::InputText("Token##media-token", mediaTokenInput.data(), mediaTokenInput.size(),
			ImGuiInputTextFlags_Password);
		ImGui::SameLine();
		ImGui::BeginDisabled(busy || mediaBusy || !mediaTokenInput[0]);
		saveMediaTokenRequested = ImGui::Button("Save securely##media-token");
		ImGui::EndDisabled();
		ImGui::SameLine();
		const std::string preferredMediaToken = mediaBackends[selectedMediaBackend].tokenEnvironment;
		ImGui::BeginDisabled(busy || mediaBusy || storedTokens.count(preferredMediaToken) == 0);
		forgetMediaTokenRequested = ImGui::Button("Forget saved token##media-token");
		ImGui::EndDisabled();
	}
	if (mediaBusy) ImGui::TextDisabled("Waiting for media endpoint...");
	ImGui::TextWrapped("%s", mediaStatus.c_str());
	if (!mediaOutput.empty()) ImGui::TextWrapped("%s", mediaOutput.c_str());
	const auto fitMediaPreview = [](float width, float height) {
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const float scale = std::min(1.0f, std::min(
			std::max(1.0f, available.x) / width,
			std::max(1.0f, available.y) / height));
		return ImVec2(width * scale, height * scale);
	};
	if (generatedImage.isAllocated()) {
		ImGui::Image(
			(ImTextureID)(uintptr_t)generatedImage.getTexture().getTextureData().textureID,
			fitMediaPreview(generatedImage.getWidth(), generatedImage.getHeight()));
	} else if (generatedVideo.isLoaded()) {
		ImGui::Image(
			(ImTextureID)(uintptr_t)generatedVideo.getTexture().getTextureData().textureID,
			fitMediaPreview(generatedVideo.getWidth(), generatedVideo.getHeight()));
	}
	ImGui::End();
	gui.end();

	if (loadDocumentRequested && !busy && !mediaBusy) {
		ofFileDialogResult selection = ofSystemLoadDialog("Load a Markdown or text document");
		if (selection.bSuccess) loadDocument(selection.getPath());
	}
	if (applyRequested) applyConfiguration();
	if (saveSettingsRequested) saveExampleSettings();
	if (resetSettingsRequested) resetExampleSettings();
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
	if (cancelRequested) cancelRequest();
	if (saveTokenRequested) {
		saveTokenCredential(endpointProfiles[selectedProfile].tokenEnvironment, tokenInput);
	}
	if (forgetTokenRequested) {
		forgetTokenCredential(endpointProfiles[selectedProfile].tokenEnvironment);
	}
	if (saveMediaTokenRequested) {
		saveTokenCredential(mediaBackends[selectedMediaBackend].tokenEnvironment, mediaTokenInput);
	}
	if (forgetMediaTokenRequested) {
		forgetTokenCredential(mediaBackends[selectedMediaBackend].tokenEnvironment);
	}
	if (generateMediaRequested) {
		if (mediaConfigurationDirty) applyMediaConfiguration();
		generateMedia();
	}
	if (pollMediaRequested) pollMediaJob();
}

void ofApp::keyPressed(int key) {
	if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard) return;
	if (key == OF_KEY_F1) {
		if (configurationDirty) applyConfiguration();
		if (!mediaBusy) inspectEndpoint();
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

void ofApp::dragEvent(ofDragInfo dragInfo) {
	if (busy || mediaBusy) {
		documentStatus = "Wait for the current request before loading documents.";
		return;
	}
	for (const auto & path : dragInfo.files) loadDocument(path.string());
}

void ofApp::exit() {
	cancellationRequested = true;
	finishWorker();
	finishMediaWorker();
}

bool ofApp::loadDocument(const std::string & path) {
	const std::string source = documentSourceName(path);
	if (source.empty() || !supportedDocumentPath(path)) {
		documentStatus = "Rejected " + (source.empty() ? std::string("document") : source) +
			": only .md and .txt files are accepted.";
		writeDocumentAutomationResult(documentStatus, loadedDocumentSources);
		return false;
	}
	if (!documents.addFile(path, source)) {
		documentStatus = "Could not load " + source +
			": it is missing, unreadable, empty, or already loaded.";
		writeDocumentAutomationResult(documentStatus, loadedDocumentSources);
		return false;
	}
	loadedDocumentSources.push_back(source);
	documentStatus = "Loaded " + source;
	writeDocumentAutomationResult(documentStatus, loadedDocumentSources);
	return true;
}

void ofApp::applySettingsToUi(const ofxICExample::ExampleSettings & settings) {
	selectedProfile = settings.endpointProfile;
	selectedMediaBackend = settings.mediaBackend;
	selectedMediaKind = supportsMediaKind(settings.mediaBackend, settings.mediaKind)
		? settings.mediaKind
		: 0;
	setTextBuffer(endpointUrl, settings.endpointUrl);
	setTextBuffer(modelId, settings.modelId);
	setTextBuffer(mediaEndpointUrl, settings.mediaEndpointUrl);
	setTextBuffer(mediaImageModel, settings.mediaImageModel);
	setTextBuffer(mediaVideoModel, settings.mediaVideoModel);
	mediaWidth = settings.mediaWidth;
	mediaHeight = settings.mediaHeight;
	mediaFrames = settings.mediaFrames;
	mediaFps = settings.mediaFps;
	configurationDirty = false;
	mediaConfigurationDirty = false;
}

ofxICExample::ExampleSettings ofApp::settingsFromUi() const {
	ofxICExample::ExampleSettings settings;
	settings.endpointProfile = selectedProfile;
	settings.endpointUrl = endpointUrl.data();
	settings.modelId = modelId.data();
	settings.mediaBackend = selectedMediaBackend;
	settings.mediaKind = selectedMediaKind;
	settings.mediaEndpointUrl = mediaEndpointUrl.data();
	settings.mediaImageModel = mediaImageModel.data();
	settings.mediaVideoModel = mediaVideoModel.data();
	settings.mediaWidth = mediaWidth;
	settings.mediaHeight = mediaHeight;
	settings.mediaFrames = mediaFrames;
	settings.mediaFps = mediaFps;
	return settings;
}

void ofApp::saveExampleSettings() {
	if (ofxICExample::saveSettings(settingsPath, settingsFromUi())) {
		settingsStatus = "Saved non-secret settings in the user profile.";
	} else {
		settingsStatus = "Could not save settings; check values and file access.";
	}
}

void ofApp::resetExampleSettings() {
	const bool removed = ofxICExample::removeSettings(settingsPath);
	if (!removed) {
		settingsStatus = "Could not remove the saved settings file.";
		return;
	}
	ofxICExample::ExampleSettings settings;
	const auto environment = settingsEnvironment();
	ofxICExample::applyEnvironmentOverrides(settings, environment);
	applySettingsToUi(settings);
	applyConfiguration();
	applyMediaConfiguration();
	settingsStatus = environment.empty()
		? "Restored built-in defaults and removed saved settings."
		: "Removed saved settings; environment overrides remain active.";
}

void ofApp::applyConfiguration() {
	if (busy || mediaBusy) return;
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

void ofApp::applyMediaConfiguration() {
	if (busy || mediaBusy) return;
	mediaEndpoint.setBaseUrl(mediaEndpointUrl.data());
	mediaEndpoint.setBearerToken(configuredMediaToken());
	mediaConfigurationDirty = false;
	currentMediaJob = {};
	mediaStatus = "Applied " + std::string(mediaBackends[selectedMediaBackend].name) +
		" at " + mediaEndpoint.getBaseUrl();
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

void ofApp::selectMediaBackend(int backendIndex) {
	if (backendIndex < 0 || backendIndex >= static_cast<int>(mediaBackends.size())) return;
	selectedMediaBackend = backendIndex;
	if (!supportsMediaKind(selectedMediaBackend, selectedMediaKind)) {
		selectedMediaKind = mediaBackends[selectedMediaBackend].supportsImage ? 0 : 1;
	}
	setTextBuffer(mediaEndpointUrl, mediaBackends[selectedMediaBackend].url);
	const std::string imageModel(mediaImageModel.data());
	if (selectedMediaBackend == 0 &&
		(imageModel.empty() || imageModel == "black-forest-labs/FLUX.1-dev")) {
		setTextBuffer(mediaImageModel, "gpt-image-2");
	} else if (selectedMediaBackend == 1 &&
		(imageModel.empty() || imageModel.compare(0, 9, "gpt-image") == 0)) {
		setTextBuffer(mediaImageModel, "black-forest-labs/FLUX.1-dev");
	}
	mediaConfigurationDirty = true;
	applyMediaConfiguration();
}

std::string ofApp::configuredToken() const {
	const std::string generic = environmentValue("OFXIC_API_KEY");
	if (!generic.empty()) return generic;
	const std::string variable = endpointProfiles[selectedProfile].tokenEnvironment;
	const std::string provider = environmentValue(variable.c_str());
	if (!provider.empty()) return provider;
	const auto stored = storedTokens.find(variable);
	if (stored != storedTokens.end()) return stored->second;
	const auto storedGeneric = storedTokens.find("OFXIC_API_KEY");
	return storedGeneric == storedTokens.end() ? std::string{} : storedGeneric->second;
}

std::string ofApp::configuredTokenSource() const {
	if (!environmentValue("OFXIC_API_KEY").empty()) return "OFXIC_API_KEY";
	const std::string variable = endpointProfiles[selectedProfile].tokenEnvironment;
	if (!environmentValue(variable.c_str()).empty()) return variable;
	if (storedTokens.count(variable)) return "Windows Credential Manager (" + variable + ")";
	if (storedTokens.count("OFXIC_API_KEY")) {
		return "Windows Credential Manager (OFXIC_API_KEY)";
	}
	return variable;
}

std::string ofApp::configuredMediaToken() const {
	const std::string mediaSpecific = environmentValue("OFXIC_MEDIA_API_KEY");
	if (!mediaSpecific.empty()) return mediaSpecific;
	const std::string providerToken = environmentValue(
		mediaBackends[selectedMediaBackend].tokenEnvironment);
	if (!providerToken.empty()) return providerToken;
	const std::string generic = environmentValue("OFXIC_API_KEY");
	if (!generic.empty()) return generic;
	const std::string variable = mediaBackends[selectedMediaBackend].tokenEnvironment;
	const auto stored = storedTokens.find(variable);
	if (stored != storedTokens.end()) return stored->second;
	const auto storedGeneric = storedTokens.find("OFXIC_API_KEY");
	return storedGeneric == storedTokens.end() ? std::string{} : storedGeneric->second;
}

std::string ofApp::configuredMediaTokenSource() const {
	if (!environmentValue("OFXIC_MEDIA_API_KEY").empty()) return "OFXIC_MEDIA_API_KEY";
	const char * providerEnvironment = mediaBackends[selectedMediaBackend].tokenEnvironment;
	if (!environmentValue(providerEnvironment).empty()) return providerEnvironment;
	if (!environmentValue("OFXIC_API_KEY").empty()) return "OFXIC_API_KEY";
	if (storedTokens.count(providerEnvironment)) {
		return "Windows Credential Manager (" + std::string(providerEnvironment) + ")";
	}
	if (storedTokens.count("OFXIC_API_KEY")) {
		return "Windows Credential Manager (OFXIC_API_KEY)";
	}
	return providerEnvironment;
}

void ofApp::inspectEndpoint() {
	if (mediaBusy || busy.exchange(true)) return;
	cancellationRequested = false;
	requestCanCancel = true;
	status = "Inspecting endpoint...";
	{
		std::lock_guard<std::mutex> lock(resultMutex);
		pendingProgressStatus.clear();
	}
	const std::string currentOutput = output;
	const std::string currentModel = chat.getOptions().model;
	worker = std::thread([this, currentOutput, currentModel]() {
		const auto inspection = endpoint.inspect([this]() {
			return cancellationRequested.load();
		});
		std::lock_guard<std::mutex> lock(resultMutex);
		pendingModels = inspection.models;
		pendingModelSelection.clear();
		if (inspection.cancelled) {
			pendingStatus = "Inspection cancelled";
		} else if (!inspection) {
			pendingStatus = "Inspection failed: " + inspection.error;
		} else if (!currentModel.empty()) {
			pendingStatus = "Endpoint reachable; configured model: " + currentModel +
				" (authentication not tested)";
		} else if (inspection.models.empty()) {
			pendingStatus = "Endpoint reachable; enter a model ID (authentication not tested)";
		} else {
			pendingModelSelection = inspection.models.front();
			pendingStatus = "Endpoint reachable; model: " + pendingModelSelection +
				" (authentication not tested)";
		}
		pendingOutput = currentOutput;
		finished = true;
	});
}

void ofApp::sendMessage() {
	if (!input[0] || mediaBusy || busy.exchange(true)) return;
	const std::string message(input.data());
	input[0] = '\0';
	lastMessage = message;
	status = "Waiting for model...";
	{
		std::lock_guard<std::mutex> lock(resultMutex);
		pendingProgressStatus = "Requesting model (request 1)...";
	}
	cancellationRequested = false;
	requestCanCancel = true;
	focusMessageInput = true;
	const std::vector<std::string> currentModels = availableModels;
	worker = std::thread([this, message, currentModels]() {
		const auto result = toolLoop.run(
			message,
			4,
			[this]() { return cancellationRequested.load(); },
			[this](const ofxIC::ToolLoopProgress & progress) {
				std::lock_guard<std::mutex> lock(resultMutex);
				if (progress.stage == ofxIC::ToolLoopStage::ExecutingTool) {
					pendingProgressStatus = "Executing allowlisted tool: " + progress.toolName;
				} else {
					pendingProgressStatus = "Requesting model (request " +
						ofToString(progress.modelRequest) + ")...";
				}
			});
		std::lock_guard<std::mutex> lock(resultMutex);
		pendingOutput = result.text;
		pendingStatus = result
			? "Inference completed with " + ofToString(result.modelRequests) + " model request(s)"
			: result.cancelled ? "Request cancelled" : "Request failed: " + result.error;
		pendingModels = currentModels;
		finished = true;
	});
}

void ofApp::cancelRequest() {
	if (!busy || !requestCanCancel) return;
	cancellationRequested = true;
	status = "Cancelling request...";
}

void ofApp::saveTokenCredential(
	const std::string & variable,
	std::array<char, 512> & input) {
	const std::string token(input.data());
	std::string error;
	if (!ofxICExample::saveCredential(variable, token, error)) {
		credentialStatus = "Could not save token: " + error;
		return;
	}
	storedTokens[variable] = token;
	input.fill('\0');
	endpoint.setBearerToken(configuredToken());
	mediaEndpoint.setBearerToken(configuredMediaToken());
	credentialStatus = "Saved " + variable + " in Windows Credential Manager.";
}

void ofApp::forgetTokenCredential(const std::string & variable) {
	std::string error;
	if (!ofxICExample::deleteCredential(variable, error)) {
		credentialStatus = "Could not forget token: " + error;
		return;
	}
	const auto stored = storedTokens.find(variable);
	if (stored != storedTokens.end()) {
		std::fill(stored->second.begin(), stored->second.end(), '\0');
		storedTokens.erase(stored);
	}
	endpoint.setBearerToken(configuredToken());
	mediaEndpoint.setBearerToken(configuredMediaToken());
	credentialStatus = "Removed saved " + variable + ". Environment overrides remain active.";
}

void ofApp::generateMedia() {
	if (!mediaInput[0] || busy) return;
	if (!supportsMediaKind(selectedMediaBackend, selectedMediaKind)) {
		mediaStatus = unsupportedMediaMessage(selectedMediaBackend, selectedMediaKind);
		mediaOutput.clear();
		writeMediaAutomationResult(mediaStatus, mediaOutput);
		return;
	}
	if (mediaBusy.exchange(true)) return;
	const std::string prompt(mediaInput.data());
	const int width = std::max(1, mediaWidth);
	const int height = std::max(1, mediaHeight);
	const int frames = std::max(1, mediaFrames);
	const int fps = std::max(1, mediaFps);
	const bool video = selectedMediaKind == 1;
	const int backend = selectedMediaBackend;
	const bool autoPoll = !environmentValue("OFXIC_MEDIA_AUTORUN").empty();
	const std::string mediaModel = video ? mediaVideoModel.data() : mediaImageModel.data();
	mediaStatus = backend == 1
		? (video ? "Submitting Hugging Face video..." : "Generating Hugging Face image...")
		: (backend == 2 ? "Submitting native media job..." : "Generating OpenAI image...");
	mediaWorker = std::thread([this, prompt, width, height, frames, fps, video, backend, mediaModel, autoPoll]() {
		std::string nextStatus;
		std::string nextOutput;
		std::string nextBase64;
		std::string nextBytes;
		std::string nextFormat;
		ofxIC::MediaJob nextJob;
		if (backend == 0) {
			ofxIC::ImageRequest request;
			request.prompt = prompt;
			request.model = mediaModel;
			request.width = width;
			request.height = height;
			const auto result = media.generateImage(request);
			nextStatus = result
				? "OpenAI image generation completed"
				: "OpenAI image generation failed: " + result.error;
			if (!result.imagesBase64.empty()) {
				nextBase64 = result.imagesBase64.front();
				nextFormat = result.outputFormat.empty() ? "png" : result.outputFormat;
				nextOutput = "Received " + ofToString(result.imagesBase64.size()) + " image payload(s)";
			} else if (!result.urls.empty()) {
				nextOutput = result.urls.front();
			}
		} else {
			ofxIC::MediaJobRequest request;
			request.kind = video ? ofxIC::MediaKind::Video : ofxIC::MediaKind::Image;
			request.prompt = prompt;
			request.model = mediaModel;
			request.width = width;
			request.height = height;
			request.videoFrames = frames;
			request.fps = fps;
			nextJob = backend == 1 ? media.submitHuggingFaceFal(request) : media.submit(request);
			for (int attempt = 0;
				autoPoll && nextJob && !nextJob.terminal() && attempt < 1200;
				++attempt) {
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				nextJob = media.poll(nextJob);
			}
			if (autoPoll && nextJob && !nextJob.terminal()) {
				nextJob.success = false;
				nextJob.error = "media automation timed out while polling";
			}
			nextStatus = nextJob
				? std::string(video ? "Video" : "Image") + " job " + nextJob.id +
					" is " + mediaJobStateLabel(nextJob.state)
				: std::string(video ? "Video" : "Image") + " request failed: " + nextJob.error;
			nextOutput = nextJob.pollUrl;
			nextFormat = nextJob.outputFormat;
			if (!nextJob.payloadBytes.empty()) {
				nextBytes = nextJob.payloadBytes.front();
				nextOutput = "Received " + ofToString(nextBytes.size()) + " media bytes";
			} else if (!nextJob.payloadsBase64.empty()) {
				nextBase64 = nextJob.payloadsBase64.front();
			}
		}
		std::lock_guard<std::mutex> lock(mediaResultMutex);
		pendingMediaStatus = std::move(nextStatus);
		pendingMediaOutput = std::move(nextOutput);
		pendingMediaBase64 = std::move(nextBase64);
		pendingMediaBytes = std::move(nextBytes);
		pendingMediaFormat = std::move(nextFormat);
		pendingMediaIsVideo = video;
		pendingMediaJob = std::move(nextJob);
		mediaFinished = true;
	});
}

void ofApp::pollMediaJob() {
	if (currentMediaJob.id.empty() || busy || mediaBusy.exchange(true)) return;
	const ofxIC::MediaJob job = currentMediaJob;
	mediaStatus = "Polling media job " + job.id + "...";
	mediaWorker = std::thread([this, job]() {
		ofxIC::MediaJob nextJob = media.poll(job);
		std::string nextStatus = nextJob
			? "Media job " + nextJob.id + " is " + mediaJobStateLabel(nextJob.state)
			: "Media job failed: " + nextJob.error;
		std::string nextOutput;
		std::string nextBase64;
		std::string nextBytes;
		if (nextJob.state == ofxIC::MediaJobState::Completed &&
			!nextJob.payloadsBase64.empty()) {
			nextBase64 = nextJob.payloadsBase64.front();
			nextOutput = "Received " + ofToString(nextJob.frameCount) + " frame(s)";
		} else if (nextJob.state == ofxIC::MediaJobState::Completed &&
			!nextJob.payloadBytes.empty()) {
			nextBytes = nextJob.payloadBytes.front();
			nextOutput = "Received " + ofToString(nextBytes.size()) + " media bytes";
		}
		std::lock_guard<std::mutex> lock(mediaResultMutex);
		pendingMediaStatus = std::move(nextStatus);
		pendingMediaOutput = std::move(nextOutput);
		pendingMediaBase64 = std::move(nextBase64);
		pendingMediaBytes = std::move(nextBytes);
		pendingMediaFormat = nextJob.outputFormat;
		pendingMediaIsVideo = nextJob.kind == ofxIC::MediaKind::Video;
		pendingMediaJob = std::move(nextJob);
		mediaFinished = true;
	});
}

void ofApp::finishWorker() {
	if (worker.joinable()) worker.join();
}

void ofApp::finishMediaWorker() {
	if (mediaWorker.joinable()) mediaWorker.join();
}
