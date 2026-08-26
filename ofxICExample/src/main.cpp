#include "ofMain.h"
#include "ofApp.h"

#if defined(_WIN32)
#include <windows.h>
#endif

int main() {
	#if defined(_WIN32)
	SetConsoleCP(CP_UTF8);
	SetConsoleOutputCP(CP_UTF8);
	#endif
	// ofxImGui logs during setup; keep an existing custom channel when available.
	if (!ofGetLoggerChannel()) ofLogToConsole();
	ofSetupOpenGL(1200, 660, OF_WINDOW);
	ofRunApp(new ofApp());
}
