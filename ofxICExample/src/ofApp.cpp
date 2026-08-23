#include "ofApp.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
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

struct MediaBackendProfile {
	const char * name;
	const char * url;
	const char * tokenEnvironment;
};

constexpr std::array<MediaBackendProfile, 3> mediaBackends{{
	{ "OpenAI images", "https://api.openai.com/v1", "OPENAI_API_KEY" },
	{ "Hugging Face / fal-ai", "https://router.huggingface.co", "HF_TOKEN" },
	{ "stable-diffusion.cpp", "http://127.0.0.1:8080", "OFXIC_API_KEY" },
}};

std::string environmentValue(const char * name) {
	const char * value = std::getenv(name);
	return value && *value ? value : "";
}

std::string configuredEndpointUrl() {
	const std::string value = environmentValue("OFXIC_ENDPOINT_URL");
	return value.empty() ? "http://127.0.0.1:8080" : value;
}

int configuredMediaBackend() {
	const std::string configured = environmentValue("OFXIC_MEDIA_BACKEND");
	if (configured == "openai") return 0;
	if (configured == "huggingface" || configured == "hf" || configured == "fal-ai") return 1;
	if (configured == "stable-diffusion.cpp" || configured == "sdcpp") return 2;
	const std::string chatUrl = configuredEndpointUrl();
	if (chatUrl.find("huggingface.co") != std::string::npos) return 1;
	if (chatUrl.find("api.openai.com") != std::string::npos) return 0;
	return 2;
}

std::string configuredMediaEndpointUrl(int backend) {
	const std::string configured = environmentValue("OFXIC_MEDIA_ENDPOINT_URL");
	if (!configured.empty()) return configured;
	if (backend >= 0 && backend < static_cast<int>(mediaBackends.size())) {
		return mediaBackends[backend].url;
	}
	return mediaBackends[2].url;
}

std::string configuredMediaModel(const char * environment, const char * fallback) {
	const std::string configured = environmentValue(environment);
	return configured.empty() ? fallback : configured;
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

} // namespace

ofApp::ofApp()
	: endpoint(configuredEndpointUrl())
	, mediaEndpoint(configuredMediaEndpointUrl(configuredMediaBackend()))
	, chat(endpoint)
	, media(mediaEndpoint)
	, toolLoop(chat, tools) {
	selectedProfile = profileForUrl(configuredEndpointUrl());
	selectedMediaBackend = configuredMediaBackend();
	setTextBuffer(endpointUrl, configuredEndpointUrl());
	setTextBuffer(mediaEndpointUrl, configuredMediaEndpointUrl(selectedMediaBackend));
	setTextBuffer(modelId, environmentValue("OFXIC_MODEL"));
	setTextBuffer(mediaImageModel, configuredMediaModel(
		"OFXIC_MEDIA_IMAGE_MODEL",
		selectedMediaBackend == 0 ? "gpt-image-2" : "black-forest-labs/FLUX.1-dev"));
	setTextBuffer(mediaVideoModel, configuredMediaModel(
		"OFXIC_MEDIA_VIDEO_MODEL", "Wan-AI/Wan2.2-TI2V-5B"));
	endpoint.setBearerToken(configuredToken());
	mediaEndpoint.setBearerToken(configuredMediaToken());
}

ofApp::~ofApp() {
	finishWorker();
	finishMediaWorker();
}

void ofApp::setup() {
	ofDisableArbTex();
	ofSetWindowTitle("ofxIC Endpoint Workbench");
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
	setTextBuffer(mediaInput, "A small paper sculpture on a clean studio background");
	mediaStatus = "Choose OpenAI images, Hugging Face / fal-ai, or stable-diffusion.cpp jobs.";
	const std::string mediaAutorun = environmentValue("OFXIC_MEDIA_AUTORUN");
	if (mediaAutorun == "image" || mediaAutorun == "video") {
		selectedMediaKind = mediaAutorun == "video" ? 1 : 0;
		const std::string prompt = environmentValue("OFXIC_MEDIA_PROMPT");
		if (!prompt.empty()) setTextBuffer(mediaInput, prompt);
		generateMedia();
	}
}

void ofApp::update() {
	if (finished.exchange(false)) {
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
	bool clearRequested = false;
	bool generateMediaRequested = false;
	bool pollMediaRequested = false;

	gui.begin();
	ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(1168, 190), ImGuiCond_FirstUseEver);
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
	ImGui::SetNextWindowSize(ImVec2(568, 426), ImGuiCond_FirstUseEver);
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

	ImGui::SetNextWindowPos(ImVec2(600, 218), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(584, 426), ImGuiCond_FirstUseEver);
	ImGui::Begin("Image and video");
	const char * backendNames[] = { "OpenAI images", "Hugging Face / fal-ai", "stable-diffusion.cpp" };
	const char * mediaKinds[] = { "Image", "Video" };
	ImGui::BeginDisabled(busy || mediaBusy);
	int nextMediaBackend = selectedMediaBackend;
	if (ImGui::Combo("Media backend", &nextMediaBackend, backendNames, 3)) {
		selectMediaBackend(nextMediaBackend);
	}
	ImGui::Combo("Kind", &selectedMediaKind, mediaKinds, 2);
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
	const bool unsupported = selectedMediaBackend == 0 && selectedMediaKind == 1;
	ImGui::BeginDisabled(unsupported);
	const char * generateLabel = selectedMediaBackend == 1
		? (selectedMediaKind == 0 ? "Generate HF image" : "Submit HF video")
		: (selectedMediaBackend == 2
			? (selectedMediaKind == 0 ? "Submit image job" : "Submit video job")
			: "Generate OpenAI image");
	generateMediaRequested = ImGui::Button(generateLabel);
	ImGui::EndDisabled();
	if (!currentMediaJob.id.empty() && !currentMediaJob.terminal()) {
		ImGui::SameLine();
		pollMediaRequested = ImGui::Button("Poll job");
	}
	ImGui::EndDisabled();
	if (unsupported) ImGui::TextDisabled("OpenAI video is not part of this compact adapter yet.");
	const std::string mediaToken = configuredMediaToken();
	ImGui::TextDisabled("Media token: %s (%s)",
		mediaToken.empty() ? "not loaded" : "loaded",
		configuredMediaTokenSource().c_str());
	if (mediaBusy) ImGui::TextDisabled("Waiting for media endpoint...");
	ImGui::TextWrapped("%s", mediaStatus.c_str());
	if (!mediaOutput.empty()) ImGui::TextWrapped("%s", mediaOutput.c_str());
	if (generatedImage.isAllocated()) {
		const float available = ImGui::GetContentRegionAvail().x;
		const float scale = std::min(1.0f, available / generatedImage.getWidth());
		ImGui::Image(
			(ImTextureID)(uintptr_t)generatedImage.getTexture().getTextureData().textureID,
			ImVec2(generatedImage.getWidth() * scale, generatedImage.getHeight() * scale));
	} else if (generatedVideo.isLoaded()) {
		const float available = ImGui::GetContentRegionAvail().x;
		const float scale = std::min(1.0f, available / generatedVideo.getWidth());
		ImGui::Image(
			(ImTextureID)(uintptr_t)generatedVideo.getTexture().getTextureData().textureID,
			ImVec2(generatedVideo.getWidth() * scale, generatedVideo.getHeight() * scale));
	}
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

void ofApp::exit() {
	finishWorker();
	finishMediaWorker();
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
	return environmentValue(endpointProfiles[selectedProfile].tokenEnvironment);
}

std::string ofApp::configuredTokenSource() const {
	if (!environmentValue("OFXIC_API_KEY").empty()) return "OFXIC_API_KEY";
	return endpointProfiles[selectedProfile].tokenEnvironment;
}

std::string ofApp::configuredMediaToken() const {
	const std::string mediaSpecific = environmentValue("OFXIC_MEDIA_API_KEY");
	if (!mediaSpecific.empty()) return mediaSpecific;
	const std::string providerToken = environmentValue(
		mediaBackends[selectedMediaBackend].tokenEnvironment);
	if (!providerToken.empty()) return providerToken;
	return environmentValue("OFXIC_API_KEY");
}

std::string ofApp::configuredMediaTokenSource() const {
	if (!environmentValue("OFXIC_MEDIA_API_KEY").empty()) return "OFXIC_MEDIA_API_KEY";
	const char * providerEnvironment = mediaBackends[selectedMediaBackend].tokenEnvironment;
	if (!environmentValue(providerEnvironment).empty()) return providerEnvironment;
	return "OFXIC_API_KEY";
}

void ofApp::inspectEndpoint() {
	if (mediaBusy || busy.exchange(true)) return;
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
	if (!input[0] || mediaBusy || busy.exchange(true)) return;
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

void ofApp::generateMedia() {
	if (!mediaInput[0] || busy || mediaBusy.exchange(true)) return;
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
