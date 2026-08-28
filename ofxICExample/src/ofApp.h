#pragma once

#include "ExampleSettings.h"
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
	void applyConfiguration();
	void applyMediaConfiguration();
	void applyMusicConfiguration();
	void applySettingsToUi(const ofxICExample::ExampleSettings & settings);
	ofxICExample::ExampleSettings settingsFromUi() const;
	void saveExampleSettings();
	void resetExampleSettings();
	void startLocalLlamaServer();
	void stopLocalLlamaServer();
	void updateLocalLlamaServer();
	void startLocalStableDiffusionServer();
	void stopLocalStableDiffusionServer();
	void startLocalAceStepServer();
	void stopLocalAceStepServer();
	void startLocalWhisperServer();
	void stopLocalWhisperServer();
	void startLocalSamBridge();
	void stopLocalSamBridge();
	bool startManagedProcess(const std::string & executable, const std::vector<std::string> & arguments,
		void *& handle, unsigned long & processId, std::string & processStatus, const std::string & name);
	void stopManagedProcess(void *& handle, unsigned long & processId, std::string & processStatus,
		const std::string & name);
	void updateManagedProcess(void *& handle, unsigned long & processId, std::string & processStatus,
		const std::string & name);
	void selectEndpointProfile(int profileIndex);
	void selectMediaBackend(int backendIndex);
	void selectMusicBackend(int backendIndex);
	void inspectEndpoint();
	void sendMessage();
	void cancelRequest();
	void saveTokenCredential(const std::string & variable, std::array<char, 512> & input);
	void forgetTokenCredential(const std::string & variable);
	bool loadDocument(const std::string & path);
	bool loadAudio(const std::string & path);
	void transcribeAudio();
	bool loadSegmentationImage(const std::string & path);
	void inspectSegmentationBridge();
	void segmentImage();
	void generateMedia();
	void pollMediaJob();
	void generateMusic();
	void pollMusicJob();
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
	std::array<char, 1024> llamaModelPath{};
	std::array<char, 1024> llamaModelDirectory{};
	std::vector<std::string> detectedLlamaModels;
	int llamaContextSize = 4096;
	int llamaGpuLayers = 999;
	bool llamaFlashAttention = true;
	void * llamaProcessHandle = nullptr;
	unsigned long llamaProcessId = 0;
	std::string llamaServerStatus = "Local llama-server is stopped.";
	std::array<char, 1024> stableDiffusionServerPath{};
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
	void * stableDiffusionProcessHandle = nullptr;
	unsigned long stableDiffusionProcessId = 0;
	std::string stableDiffusionServerStatus = "Local sd-server is stopped.";
	std::array<char, 1024> aceStepServerPath{};
	std::array<char, 2048> aceStepServerArguments{};
	void * aceStepProcessHandle = nullptr;
	unsigned long aceStepProcessId = 0;
	std::string aceStepServerStatus = "Local ACE-Step server is stopped.";
	std::array<char, 1024> whisperServerPath{};
	std::array<char, 1024> whisperModelPath{};
	std::array<char, 2048> whisperServerArguments{};
	void * whisperProcessHandle = nullptr;
	unsigned long whisperProcessId = 0;
	std::string whisperServerStatus = "Local whisper.cpp server is stopped.";
	std::array<char, 1024> samBridgeExecutablePath{};
	std::array<char, 2048> samBridgeArguments{};
	void * samBridgeProcessHandle = nullptr;
	unsigned long samBridgeProcessId = 0;
	std::string samBridgeProcessStatus = "Local SAM bridge is stopped.";
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
	std::string credentialStatus;
	std::map<std::string, std::string> storedTokens;
	std::string lastMessage;
	std::string output;
	std::string status;
	std::string documentStatus;
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
	std::string pendingMediaOutput;
	std::string pendingMediaBase64;
	std::string pendingMediaBytes;
	std::string pendingMediaFormat;
	std::string musicStatus;
	std::string musicOutput;
	std::string pendingMusicStatus;
	std::string pendingMusicOutput;
	std::string pendingMusicBytes;
	std::string pendingMusicFormat;
	bool pendingMediaIsVideo = false;
	ofxIC::MediaJob currentMediaJob;
	ofxIC::MediaJob pendingMediaJob;
	ofxIC::StabilityAudioJob currentMusicJob;
	ofxIC::StabilityAudioJob pendingMusicJob;
	ofxIC::AceStepMusicJob currentAceStepMusicJob;
	ofxIC::AceStepMusicJob pendingAceStepMusicJob;
	ofImage segmentationImage;
	ofImage segmentationMaskImage;
	ofImage generatedImage;
	ofVideoPlayer generatedVideo;
	ofSoundPlayer generatedMusic;
	std::atomic<bool> mediaBusy{ false };
	std::atomic<bool> mediaFinished{ false };
	std::atomic<bool> musicFinished{ false };
};
