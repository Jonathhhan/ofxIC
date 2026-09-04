#include "ExampleMediaContextPolicy.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace ofxICExample {
namespace {

void appendSignatureField(std::ostringstream & output, const std::string & value) {
	output << value.size() << ':' << value << ';';
}

std::string normalizedFilename(std::string value) {
	const std::size_t separator = value.find_last_of("/\\");
	if (separator != std::string::npos) value.erase(0, separator + 1U);
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

void keepSupported(std::string & selected, const std::vector<std::string> & supported) {
	if (!selected.empty() &&
		std::find(supported.begin(), supported.end(), selected) == supported.end())
		selected.clear();
}

} // namespace

std::string mediaRuntimeSignature(const MediaRuntimeConfig & config) {
	std::ostringstream output;
	appendSignatureField(output, config.serverPath);
	appendSignatureField(output, config.modelPath);
	appendSignatureField(output, config.vaePath);
	appendSignatureField(output, config.clipLPath);
	appendSignatureField(output, config.clipGPath);
	appendSignatureField(output, config.textEncoderPath);
	output << (config.completeCheckpoint ? '1' : '0')
		<< (config.flashAttention ? '1' : '0')
		<< (config.offloadToCpu ? '1' : '0');
	return output.str();
}

bool mediaModelMatches(const std::string & loadedModel, const std::string & selectedPath) {
	const std::string loaded = normalizedFilename(loadedModel);
	const std::string selected = normalizedFilename(selectedPath);
	return !loaded.empty() && !selected.empty() && loaded == selected;
}

void reconcileMediaControls(const ofxIC::MediaCapabilities & capabilities,
	bool contextMatchesSelection, MediaControlSelection & selection) {
	if (!capabilities || !contextMatchesSelection) return;
	const bool image = capabilities.supports(ofxIC::MediaKind::Image);
	const bool video = capabilities.supports(ofxIC::MediaKind::Video);
	if (image != video) selection.kind = video ? 1 : 0;
	keepSupported(selection.sampler, capabilities.samplers);
	keepSupported(selection.scheduler, capabilities.schedulers);
	keepSupported(selection.outputFormat, selection.kind == 1
		? capabilities.videoOutputFormats : capabilities.imageOutputFormats);
}

void applySafeMediaDefaults(const ofxIC::MediaCapabilities & capabilities,
	int & width, int & height, int & frames, int & fps) {
	if (!capabilities) return;
	if (capabilities.defaultWidth > 0) width = capabilities.defaultWidth;
	if (capabilities.defaultHeight > 0) height = capabilities.defaultHeight;
	if (capabilities.defaultVideoFrames > 1) frames = capabilities.defaultVideoFrames;
	if (capabilities.defaultFps > 0) fps = capabilities.defaultFps;
}

} // namespace ofxICExample
