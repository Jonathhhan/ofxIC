#include "ofMain.h"
#include "ofApp.h"

int main() {
	// ofxImGui logs during setup; keep an existing custom channel when available.
	if (!ofGetLoggerChannel()) ofLogToConsole();
	ofSetupOpenGL(1200, 660, OF_WINDOW);
	ofRunApp(new ofApp());
}
