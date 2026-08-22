#pragma once

#include "ofMain.h"
#include "ofxIC.h"
#include "ofxImGui.h"

#include <array>
#include <atomic>
#include <mutex>
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
	void exit() override;

private:
	void applyConfiguration();
	void applyMediaConfiguration();
	void selectEndpointProfile(int profileIndex);
	void selectMediaBackend(int backendIndex);
	void inspectEndpoint();
	void sendMessage();
	void generateMedia();
	void pollMediaJob();
	void finishWorker();
	void finishMediaWorker();
	std::string configuredToken() const;
	std::string configuredTokenSource() const;
	std::string configuredMediaToken() const;
	std::string configuredMediaTokenSource() const;

	ofxImGui::Gui gui;
	ofxIC::Endpoint endpoint;
	ofxIC::Endpoint mediaEndpoint;
	ofxIC::ChatSession chat;
	ofxIC::MediaClient media;
	ofxIC::DocumentIndex documents;
	ofxIC::ToolRegistry tools;
	ofxIC::ToolLoop toolLoop;
	std::array<char, 512> endpointUrl{};
	std::array<char, 512> mediaEndpointUrl{};
	std::array<char, 256> modelId{};
	std::array<char, 256> mediaImageModel{};
	std::array<char, 256> mediaVideoModel{};
	std::array<char, 2048> input{};
	std::array<char, 2048> mediaInput{};
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
	std::string lastMessage;
	std::string output;
	std::string status;
	std::vector<std::string> availableModels;
	std::thread worker;
	std::mutex resultMutex;
	std::string pendingOutput;
	std::string pendingStatus;
	std::string pendingModelSelection;
	std::vector<std::string> pendingModels;
	std::atomic<bool> busy{ false };
	std::atomic<bool> finished{ false };
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
	ofImage generatedImage;
	ofVideoPlayer generatedVideo;
	std::atomic<bool> mediaBusy{ false };
	std::atomic<bool> mediaFinished{ false };
};
