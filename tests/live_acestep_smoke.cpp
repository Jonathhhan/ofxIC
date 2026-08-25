#include "../src/ofxIC.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char ** argv) {
	const std::string endpointUrl = argc > 1 ? argv[1] : "http://127.0.0.1:8085";
	const std::string outputPath = argc > 2 ? argv[2] : "ofxIC-acestep-live.wav";

	ofxIC::Endpoint endpoint(endpointUrl);
	ofxIC::AceStepMusicClient music(endpoint);
	ofxIC::AceStepMusicRequest request;
	request.caption = "short warm ambient electronic pulse, sparse and clean";
	request.durationSeconds = 4;
	request.seed = 42;
	request.outputFormat = "wav";

	ofxIC::AceStepMusicJob job = music.submit(request);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(20);
	while (job && !job.terminal() && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		job = music.poll(job);
	}
	if (!job) {
		std::cerr << "ACE-Step live smoke failed: " << job.error << '\n';
		return 1;
	}
	if (job.state != ofxIC::AceStepMusicJobState::Completed || job.audioBytes.empty()) {
		std::cerr << "ACE-Step live smoke timed out before audio completed\n";
		return 2;
	}
	std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
	output.write(job.audioBytes.data(), static_cast<std::streamsize>(job.audioBytes.size()));
	if (!output) {
		std::cerr << "Could not write live smoke audio to " << outputPath << '\n';
		return 3;
	}
	std::cout << "ACE-Step live smoke passed: " << job.audioBytes.size()
		<< " WAV bytes -> " << outputPath << '\n';
	return 0;
}
