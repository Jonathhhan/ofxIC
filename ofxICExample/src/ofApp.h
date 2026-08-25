#pragma once

#include "ExampleSettings.h"
#include "ExampleCredentialStore.h"
#include "ofMain.h"
#include "ofxIC.h"
#include "ofxImGui.h"

#include <array>
#include <atomic>
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
	void applySettingsToUi(const ofxICExample::ExampleSettings & settings);
	ofxICExample::ExampleSettings settingsFromUi() const;
	void saveExampleSettings();
	void resetExampleSettings();
	void selectEndpointProfile(int profileIndex);
	void selectMediaBackend(int backendIndex);
	void inspectEndpoint();
	void sendMessage();
	void cancelRequest();
	void saveTokenCredential(const std::string & variable, std::array<char, 512> & input);
	void forgetTokenCredential(const std::string & variable);
	bool loadDocument(const std::string & path);
	bool loadAudio(const std::string & path);
	void transcribeAudio();
	bool loadSegmentationImage(const std::string & path);
	void segmentImage();
	void generateMedia();
	void pollMediaJob();
	void finishWorker();
	void finishMediaWorker();
	std::string configuredToken() const;
	std::string configuredTokenSource() const;
	std::string configuredTranscriptionToken() const;
	std::string configuredSegmentationToken() const;
	std::string configuredMediaToken() const;
	std::string configuredMediaTokenSource() const;

	ofxImGui::Gui gui;
	ofxIC::Endpoint endpoint;
	ofxIC::Endpoint transcriptionEndpoint;
	ofxIC::Endpoint segmentationEndpoint;
	ofxIC::Endpoint mediaEndpoint;
	ofxIC::ChatSession chat;
	ofxIC::MediaClient media;
	ofxIC::TranscriptionClient transcription;
	ofxIC::SegmentationClient segmentation;
	ofxIC::DocumentIndex documents;
	ofxIC::ToolRegistry tools;
	ofxIC::ToolLoop toolLoop;
	std::array<char, 512> endpointUrl{};
	std::array<char, 512> transcriptionEndpointUrl{};
	std::array<char, 512> segmentationEndpointUrl{};
	std::array<char, 512> mediaEndpointUrl{};
	std::array<char, 256> modelId{};
	std::array<char, 256> transcriptionModel{};
	std::array<char, 256> mediaImageModel{};
	std::array<char, 256> mediaVideoModel{};
	std::array<char, 2048> input{};
	std::array<char, 2048> mediaInput{};
	std::array<char, 512> tokenInput{};
	std::array<char, 512> mediaTokenInput{};
	int selectedProfile = 0;
	int selectedMediaBackend = 0;
	int selectedMediaKind = 0;
	int mediaWidth = 512;
	int mediaHeight = 512;
	int mediaFrames = 33;
	int mediaFps = 16;
	bool configurationDirty = false;
	bool mediaConfigurationDirty = false;
	bool focusMessageInput = true;
	std::string settingsPath;
	std::string settingsStatus;
	std::string credentialStatus;
	std::map<std::string, std::string> storedTokens;
	std::string lastMessage;
	std::string output;
	std::string status;
	std::string documentStatus;
	std::vector<std::string> loadedDocumentSources;
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
	std::string pendingSegmentationMask;
	std::string pendingModelSelection;
	std::vector<std::string> pendingModels;
	std::atomic<bool> busy{ false };
	std::atomic<bool> finished{ false };
	std::atomic<bool> cancellationRequested{ false };
	std::atomic<bool> requestCanCancel{ false };
	std::thread mediaWorker;
	std::mutex mediaResultMutex;
	std::string mediaStatus;
	std::string mediaOutput;
	std::string pendingMediaStatus;
	std::string pendingMediaOutput;
	std::string pendingMediaBase64;
	std::string pendingMediaBytes;
	std::string pendingMediaFormat;
	bool pendingMediaIsVideo = false;
	ofxIC::MediaJob currentMediaJob;
	ofxIC::MediaJob pendingMediaJob;
	ofImage segmentationImage;
	ofImage segmentationMaskImage;
	ofImage generatedImage;
	ofVideoPlayer generatedVideo;
	std::atomic<bool> mediaBusy{ false };
	std::atomic<bool> mediaFinished{ false };
};
