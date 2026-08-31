#pragma once

#include "ofMain.h"
#include "ofxIC.h"
#include "ofxImGui.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class ofApp : public ofBaseApp {
public:
	~ofApp() override;

	void setup() override;
	void update() override;
	void draw() override;
	void exit() override;

private:
	void sendMessage();
	void stopWorker();

	ofxIC::Endpoint endpoint;
	ofxIC::ChatSession chat{ endpoint };
	ofxImGui::Gui gui;
	std::thread worker;
	std::atomic<bool> busy{ false };
	std::atomic<bool> cancelRequested{ false };
	std::mutex resultMutex;
	std::string pendingStatus;
	std::string pendingAnswer;
	bool resultReady = false;

	std::string input;
	std::string status = "Type a message and press Enter.";
	std::string answer;
};
