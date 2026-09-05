#pragma once

#include "ExampleSettings.h"
#include "ExampleManagedProcess.h"
#include "ExampleJobHistory.h"
#include "ExampleCredentialStore.h"
#include "ofMain.h"
#include "ofxIC.h"
#include "ofxImGui.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <map>
#include <string>
#include <thread>
#include <vector>

class ofApp : public ofBaseApp {
public:
	ofApp();
	~ofApp() override;

	void setup() override;
	void update() override;
	void draw() override;
	void keyPressed(int key) override;
	void dragEvent(ofDragInfo dragInfo) override;
	void exit() override;

private:
	enum class DeferredTask {
		None,
		Chat,
		LlamaInspect,
		Media,
		Transcription,
		Music,
		SamInspect,
		SamSegment
	};

	void applyConfiguration();
	void applyMediaConfiguration();
	void applyMusicConfiguration();
	void applySettingsToUi(const ofxICExample::ExampleSettings & settings);
	void applyLocalRuntimeDefaults();
	void rescanInstalledRuntimes();
	ofxICExample::ExampleSettings settingsFromUi() const;
	void saveExampleSettings();
	void resetExampleSettings();
	void startLocalLlamaServer();
	void stopLocalLlamaServer();
	void updateLocalLlamaServer();
	void startLocalStableDiffusionServer();
	void stopLocalStableDiffusionServer();
	std::string stableDiffusionRuntimeSignature() const;
	void startLocalAceStepServer();
	void stopLocalAceStepServer();
	void startLocalWhisperServer();
	void stopLocalWhisperServer();
	void startLocalSamBridge();
	void stopLocalSamBridge();
	void updateManagedProcess(ofxICExample::ManagedProcess & process,
		std::string & processStatus, const std::string & logName);
	void configureRuntimeAutomation();
	void updateRuntimeAutomation();
	void finishRuntimeAutomation(const std::string & state,
		const std::string & status);
	bool deferUntilRuntimeReady(DeferredTask task);
	void continueDeferredTask();
	void cancelDeferredTask();
	void setDeferredTaskStatus(const std::string & message);
	void writeDeferredTaskAutomationResult(const std::string & message);
	const char * deferredTaskLabel(DeferredTask task) const;
	const char * deferredTaskHistoryKind(DeferredTask task) const;
	void recordTaskHistory(const std::string & task, const std::string & detail,
		const std::string & outputPath = {});
	std::string diagnosticsReport() const;
	bool exportDiagnostics(const std::string & path);
	void selectEndpointProfile(int profileIndex);
	void selectMediaBackend(int backendIndex);
	void applyInferredMediaKind(const std::string & modelPath);
	void selectMusicBackend(int backendIndex);
	void inspectEndpoint();
	void sendMessage();
	void cancelRequest();
	void saveTokenCredential(const std::string & variable, std::array<char, 512> & input);
	void forgetTokenCredential(const std::string & variable);
	bool loadDocument(const std::string & path);
	void startWebImport();
	void updateWebImport();
	void discardWebImport();
	void acceptWebImport();
	void writeWebImportEvidence();
	bool loadAudio(const std::string & path);
	void transcribeAudio();
	bool loadSegmentationImage(const std::string & path);
	void inspectSegmentationBridge();
	void segmentImage();
	void generateMedia();
	void inspectMediaContext();
	void reconcileCurrentMediaControls();
	void generateMusic();
	void rebuildMusicWaveform(const std::string & bytes, const std::string & format);
	void failWorker(const std::string & task, const std::string & error);
	void failMediaWorker(const std::string & task, const std::string & error);
	void failMusicWorker(const std::string & task, const std::string & error);
	void finishWorker();
	void finishMediaWorker();
	std::string configuredToken() const;
	std::string configuredTokenSource() const;
	std::string configuredTranscriptionToken() const;
	std::string configuredSegmentationToken() const;
	std::string configuredMediaToken() const;
	std::string configuredMediaTokenSource() const;
	std::string configuredMusicToken() const;
	std::string configuredMusicTokenSource() const;

	ofxImGui::Gui gui;
	ofxIC::Endpoint endpoint;
	ofxIC::Endpoint transcriptionEndpoint;
	ofxIC::Endpoint segmentationEndpoint;
	ofxIC::Endpoint mediaEndpoint;
	ofxIC::Endpoint musicEndpoint;
	ofxIC::ChatSession chat;
	ofxIC::MediaClient media;
	ofxIC::StabilityAudioClient stabilityMusic;
	ofxIC::AceStepMusicClient aceStepMusic;
	ofxIC::TranscriptionClient transcription;
	ofxIC::SegmentationClient segmentation;
	ofxIC::DocumentIndex documents;
	ofxIC::ToolRegistry tools;
	ofxIC::ToolLoop toolLoop;
	std::array<char, 512> endpointUrl{};
	std::array<char, 512> transcriptionEndpointUrl{};
	std::array<char, 512> segmentationEndpointUrl{};
	std::array<char, 512> mediaEndpointUrl{};
	std::array<char, 512> musicEndpointUrl{};
	std::array<char, 256> modelId{};
	std::array<char, 2048> chatSystemPrompt{};
	std::array<char, 1024> chatStopSequences{};
	int chatMaxTokens = 512;
	float chatTemperature = 0.7f;
	float chatTopP = 0.95f;
	int chatSeed = -1;
	std::array<char, 1024> llamaServerPath{};
	std::string detectedLlamaServerPath;
	std::array<char, 1024> llamaModelPath{};
	std::array<char, 1024> llamaModelDirectory{};
	std::vector<std::string> detectedLlamaModels;
	int llamaContextSize = 4096;
	int llamaGpuLayers = 999;
	bool llamaFlashAttention = true;
	ofxICExample::ManagedProcess llamaProcess;
	std::string llamaServerStatus = "Local llama-server is stopped.";
	std::array<char, 1024> stableDiffusionServerPath{};
	std::string detectedStableDiffusionServerPath;
	std::array<char, 1024> stableDiffusionModelPath{};
	std::array<char, 1024> stableDiffusionVaePath{};
	std::array<char, 1024> stableDiffusionTextEncoderPath{};
	std::array<char, 1024> stableDiffusionClipLPath{};
	std::array<char, 1024> stableDiffusionClipGPath{};
	std::array<char, 1024> stableDiffusionModelDirectory{};
	std::vector<std::string> detectedDiffusionModels;
	std::vector<std::string> detectedVaeModels;
	std::vector<std::string> detectedTextEncoders;
	bool stableDiffusionFlashAttention = true;
	bool stableDiffusionCompleteCheckpoint = false;
	bool stableDiffusionOffloadToCpu = false;
	ofxICExample::ManagedProcess stableDiffusionProcess;
	std::string stableDiffusionActiveModelPath;
	std::string stableDiffusionActiveRuntimeSignature;
	std::string stableDiffusionServerStatus = "Local sd-server is stopped.";
	std::array<char, 1024> aceStepServerPath{};
	std::string detectedAceStepServerPath;
	std::array<char, 2048> aceStepServerArguments{};
	std::array<char, 1024> aceStepModelDirectory{};
	ofxICExample::ManagedProcess aceStepProcess;
	std::string aceStepServerStatus = "Local ACE-Step server is stopped.";
	std::array<char, 1024> whisperServerPath{};
	std::string detectedWhisperServerPath;
	std::array<char, 1024> whisperModelPath{};
	std::array<char, 2048> whisperServerArguments{};
	ofxICExample::ManagedProcess whisperProcess;
	std::string whisperServerStatus = "Local whisper.cpp server is stopped.";
	std::array<char, 1024> samBridgeExecutablePath{};
	std::string detectedSamPythonPath;
	std::array<char, 2048> samBridgeArguments{};
	std::array<char, 1024> samRunnerPath{};
	std::string detectedSamRunnerPath;
	std::array<char, 1024> samModelPath{};
	bool samCuda = true;
	ofxICExample::ManagedProcess samBridgeProcess;
	std::string samBridgeProcessStatus = "Local SAM bridge is stopped.";
	std::string runtimeAutomation;
	std::string runtimeAutomationResultPath;
	std::uint64_t runtimeAutomationStartedAtMillis = 0;
	std::uint64_t runtimeAutomationTimeoutMillis = 900000;
	bool runtimeAutomationPlanOnly = false;
	bool runtimeAutomationFinished = false;
	std::string runtimeSetupStatus =
		"Fixed versions are defined in the scripts; pinning alone does not prove compatibility.";
	std::array<char, 256> transcriptionModel{};
	std::array<char, 256> mediaImageModel{};
	std::array<char, 256> mediaVideoModel{};
	std::array<char, 2048> input{};
	std::array<char, 2048> mediaInput{};
	std::array<char, 2048> musicInput{};
	std::array<char, 512> tokenInput{};
	std::array<char, 512> mediaTokenInput{};
	std::array<char, 512> musicTokenInput{};
	int selectedProfile = 0;
	int selectedMediaBackend = 0;
	int selectedMusicBackend = 0;
	int selectedMediaKind = 0;
	int mediaWidth = 512;
	int mediaHeight = 512;
	int mediaFrames = 33;
	int mediaFps = 16;
	int mediaSeed = -1;
	int mediaSteps = 28;
	float mediaGuidance = 7.0f;
	std::string mediaSampler;
	std::string mediaScheduler;
	std::string mediaOutputFormat;
	int musicDuration = 30;
	int musicOutputFormat = 0;
	bool configurationDirty = false;
	bool mediaConfigurationDirty = false;
	bool musicConfigurationDirty = false;
	bool focusMessageInput = true;
	bool streamChat = false;
	std::string pendingChatAutorun;
	bool pendingInspectAutorun = false;
	std::string settingsPath;
	std::string settingsStatus;
	std::string diagnosticsStatus = "No diagnostic report exported.";
	std::string historyPath;
	ofxICExample::JobHistory jobHistory{ 100 };
	DeferredTask deferredTask = DeferredTask::None;
	std::uint64_t deferredTaskDeadlineMillis = 0;
	std::string activeTaskKind;
	std::string activeMediaTaskKind;
	std::string activeMusicTaskKind;
	std::string credentialStatus;
	std::map<std::string, std::string> storedTokens;
	std::string lastMessage;
	std::string output;
	std::string status;
	std::string documentStatus;
	std::array<char, 8193> webImportUrl{};
	ofxICExample::ManagedProcess webImportProcess;
	std::string webImportPath;
	std::string webImportText;
	std::string webImportStatus = "Import one public webpage; review it before adding it to Documents.";
	bool webImportPending = false;
	std::size_t webImportDocumentsBefore = 0;
	std::uint64_t webImportDeadlineMillis = 0;
	std::vector<std::string> loadedDocumentSources;
	std::vector<std::string> loadedDocumentContents;
	int selectedDocument = 0;
	std::vector<std::string> availableModels;
	std::string audioBytes;
	std::string audioFilename;
	std::string audioStatus = "No audio loaded.";
	int transcriptionProtocol = 0;
	std::string segmentationImageBytes;
	std::string segmentationFilename;
	std::string segmentationStatus = "No segmentation image loaded.";
	float segmentationPointX = 0.5f;
	float segmentationPointY = 0.5f;
	std::vector<ofxIC::SegmentationPoint> segmentationPoints;
	std::thread worker;
	std::mutex resultMutex;
	std::string pendingOutput;
	std::string pendingStatus;
	std::string pendingProgressStatus;
	std::string pendingStreamOutput;
	std::string pendingSegmentationMask;
	std::string pendingModelSelection;
	std::vector<std::string> pendingModels;
	std::atomic<bool> busy{ false };
	std::atomic<bool> finished{ false };
	std::atomic<bool> cancellationRequested{ false };
	std::atomic<bool> requestCanCancel{ false };
	std::uint64_t automationCancelAtMillis = 0;
	std::thread mediaWorker;
	std::mutex mediaResultMutex;
	std::string mediaStatus;
	std::string mediaOutput;
	std::string pendingMediaStatus;
	std::string pendingMediaProgressStatus;
	std::string pendingMediaOutput;
	std::string pendingMediaSavedPath;
	std::string musicStatus;
	std::string musicOutput;
	std::string pendingMusicStatus;
	std::string pendingMusicOutput;
	std::string pendingMusicBytes;
	std::string pendingMusicFormat;
	bool pendingMediaIsVideo = false;
	ofxIC::MediaJob currentMediaJob;
	ofxIC::MediaJob pendingMediaJob;
	ofxIC::MediaCapabilities currentMediaCapabilities;
	ofxIC::MediaCapabilities pendingMediaCapabilities;
	bool pendingMediaCapabilitiesReady = false;
	ofxIC::MediaCapabilities pendingRuntimeMediaCapabilities;
	bool pendingRuntimeMediaCapabilitiesReady = false;
	ofxIC::StabilityAudioJob currentMusicJob;
	ofxIC::StabilityAudioJob pendingMusicJob;
	ofxIC::AceStepMusicJob currentAceStepMusicJob;
	ofxIC::AceStepMusicJob pendingAceStepMusicJob;
	ofImage segmentationImage;
	ofImage segmentationMaskImage;
	ofImage generatedImage;
	ofVideoPlayer generatedVideo;
	ofSoundPlayer generatedMusic;
	std::vector<float> musicWaveformPeaks;
	float musicPlaybackPosition = 0.0f;
	float musicPlaybackDuration = 0.0f;
	bool musicWaveformAvailable = false;
	bool musicPlaybackPaused = false;
	std::atomic<bool> mediaBusy{ false };
	std::atomic<bool> mediaFinished{ false };
	std::atomic<bool> musicFinished{ false };
	std::string guiHeartbeatPath;
	std::uint64_t guiHeartbeatFrames = 0;
	std::uint64_t guiHeartbeatLastWriteMillis = 0;
};
