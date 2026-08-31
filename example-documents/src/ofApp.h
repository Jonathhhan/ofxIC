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
	void askQuestion();
	void stopWorker();

	ofxIC::Endpoint endpoint;
	ofxIC::ChatSession chat{ endpoint };
	ofxIC::DocumentIndex documents;
	ofxIC::ToolRegistry tools;
	ofxIC::ToolLoop loop{ chat, tools };
	ofxImGui::Gui gui;

	std::thread worker;
	std::atomic<bool> busy{ false };
	std::atomic<bool> cancelRequested{ false };
	std::mutex resultMutex;
	std::string pendingStatus;
	std::string pendingAnswer;
	bool statusReady = false;
	bool resultReady = false;

	std::string documentStatus;
	std::string input;
	std::string status = "Type a question and press Enter.";
	std::string answer;
};
