#include "ExampleSettings.h"

#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace ofxICExample {
namespace {

constexpr int settingsVersion = 1;

bool parseString(const std::string & value, std::string & destination) {
	if (value.empty() || value.front() != '"') return false;
	std::istringstream stream(value);
	std::string parsed;
	if (!(stream >> std::quoted(parsed))) return false;
	stream >> std::ws;
	if (!stream.eof()) return false;
	destination = std::move(parsed);
	return true;
}

bool parseInt(const std::string & value, int & destination) {
	std::istringstream stream(value);
	int parsed = 0;
	if (!(stream >> parsed)) return false;
	stream >> std::ws;
	if (!stream.eof()) return false;
	destination = parsed;
	return true;
}

bool validText(const std::string & value, std::size_t maximumSize) {
	return value.size() < maximumSize &&
		value.find_first_of("\r\n") == std::string::npos;
}

bool validSettings(const ExampleSettings & settings) {
	return settings.endpointProfile >= 0 && settings.endpointProfile <= 4 &&
		validText(settings.endpointUrl, 512) && validText(settings.modelId, 256) &&
		validText(settings.transcriptionEndpointUrl, 512) &&
		settings.transcriptionProtocol >= 0 && settings.transcriptionProtocol <= 1 &&
		validText(settings.transcriptionModel, 256) &&
		validText(settings.segmentationEndpointUrl, 512) &&
		settings.mediaBackend >= 0 && settings.mediaBackend <= 2 &&
		settings.mediaKind >= 0 && settings.mediaKind <= 1 &&
		validText(settings.mediaEndpointUrl, 512) &&
		validText(settings.mediaImageModel, 256) &&
		validText(settings.mediaVideoModel, 256) &&
		settings.mediaWidth >= 1 && settings.mediaWidth <= 8192 &&
		settings.mediaHeight >= 1 && settings.mediaHeight <= 8192 &&
		settings.mediaFrames >= 1 && settings.mediaFrames <= 10000 &&
		settings.mediaFps >= 1 && settings.mediaFps <= 240;
}

std::string normalizedUrl(std::string url) {
	while (!url.empty() && url.back() == '/') url.pop_back();
	if (url.size() >= 3 && url.compare(url.size() - 3, 3, "/v1") == 0) {
		url.erase(url.size() - 3);
	}
	return url;
}

int profileForUrl(const std::string & url) {
	const std::string normalized = normalizedUrl(url);
	if (normalized == "http://127.0.0.1:8080") return 0;
	if (normalized == "http://127.0.0.1:1234") return 1;
	if (normalized == "https://router.huggingface.co") return 2;
	if (normalized == "https://api.openai.com") return 3;
	return 4;
}

bool readPositiveOverride(
	const std::map<std::string, std::string> & environment,
	const char * name,
	int maximum,
	int & destination) {
	const auto found = environment.find(name);
	if (found == environment.end() || found->second.empty()) return false;
	int parsed = 0;
	if (!parseInt(found->second, parsed) || parsed < 1 || parsed > maximum) return false;
	destination = parsed;
	return true;
}

const std::string * environmentValue(
	const std::map<std::string, std::string> & environment,
	const char * name) {
	const auto found = environment.find(name);
	return found == environment.end() || found->second.empty() ? nullptr : &found->second;
}

} // namespace

SettingsLoadStatus loadSettings(const std::string & path, ExampleSettings & settings) {
	std::ifstream input(path, std::ios::binary);
	if (!input) return SettingsLoadStatus::Missing;

	ExampleSettings parsed;
	bool hasVersion = false;
	std::string line;
	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) continue;
		const std::size_t separator = line.find('=');
		if (separator == std::string::npos) return SettingsLoadStatus::Invalid;
		const std::string key = line.substr(0, separator);
		const std::string value = line.substr(separator + 1);
		bool valid = true;
		if (key == "version") {
			int version = 0;
			valid = parseInt(value, version) && version == settingsVersion;
			hasVersion = valid;
		} else if (key == "endpoint_profile") {
			valid = parseInt(value, parsed.endpointProfile);
		} else if (key == "endpoint_url") {
			valid = parseString(value, parsed.endpointUrl);
		} else if (key == "model_id") {
			valid = parseString(value, parsed.modelId);
		} else if (key == "transcription_endpoint_url") {
			valid = parseString(value, parsed.transcriptionEndpointUrl);
		} else if (key == "transcription_protocol") {
			valid = parseInt(value, parsed.transcriptionProtocol);
		} else if (key == "transcription_model") {
			valid = parseString(value, parsed.transcriptionModel);
		} else if (key == "segmentation_endpoint_url") {
			valid = parseString(value, parsed.segmentationEndpointUrl);
		} else if (key == "media_backend") {
			valid = parseInt(value, parsed.mediaBackend);
		} else if (key == "media_kind") {
			valid = parseInt(value, parsed.mediaKind);
		} else if (key == "media_endpoint_url") {
			valid = parseString(value, parsed.mediaEndpointUrl);
		} else if (key == "media_image_model") {
			valid = parseString(value, parsed.mediaImageModel);
		} else if (key == "media_video_model") {
			valid = parseString(value, parsed.mediaVideoModel);
		} else if (key == "media_width") {
			valid = parseInt(value, parsed.mediaWidth);
		} else if (key == "media_height") {
			valid = parseInt(value, parsed.mediaHeight);
		} else if (key == "media_frames") {
			valid = parseInt(value, parsed.mediaFrames);
		} else if (key == "media_fps") {
			valid = parseInt(value, parsed.mediaFps);
		}
		if (!valid) return SettingsLoadStatus::Invalid;
	}
	if (!input.eof() || !hasVersion || !validSettings(parsed)) {
		return SettingsLoadStatus::Invalid;
	}
	settings = std::move(parsed);
	return SettingsLoadStatus::Loaded;
}

bool saveSettings(const std::string & path, const ExampleSettings & settings) {
	if (path.empty() || !validSettings(settings)) return false;
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output) return false;
	output << "version=" << settingsVersion << '\n'
		<< "endpoint_profile=" << settings.endpointProfile << '\n'
		<< "endpoint_url=" << std::quoted(settings.endpointUrl) << '\n'
		<< "model_id=" << std::quoted(settings.modelId) << '\n'
		<< "transcription_endpoint_url=" << std::quoted(settings.transcriptionEndpointUrl) << '\n'
		<< "transcription_protocol=" << settings.transcriptionProtocol << '\n'
		<< "transcription_model=" << std::quoted(settings.transcriptionModel) << '\n'
		<< "segmentation_endpoint_url=" << std::quoted(settings.segmentationEndpointUrl) << '\n'
		<< "media_backend=" << settings.mediaBackend << '\n'
		<< "media_kind=" << settings.mediaKind << '\n'
		<< "media_endpoint_url=" << std::quoted(settings.mediaEndpointUrl) << '\n'
		<< "media_image_model=" << std::quoted(settings.mediaImageModel) << '\n'
		<< "media_video_model=" << std::quoted(settings.mediaVideoModel) << '\n'
		<< "media_width=" << settings.mediaWidth << '\n'
		<< "media_height=" << settings.mediaHeight << '\n'
		<< "media_frames=" << settings.mediaFrames << '\n'
		<< "media_fps=" << settings.mediaFps << '\n';
	return static_cast<bool>(output);
}

bool removeSettings(const std::string & path) {
	if (path.empty()) return false;
	std::ifstream existing(path, std::ios::binary);
	if (!existing) return true;
	existing.close();
	return std::remove(path.c_str()) == 0;
}

void applyEnvironmentOverrides(
	ExampleSettings & settings,
	const std::map<std::string, std::string> & environment) {
	const std::string * endpointUrl = environmentValue(environment, "OFXIC_ENDPOINT_URL");
	if (endpointUrl) {
		settings.endpointUrl = *endpointUrl;
		settings.endpointProfile = profileForUrl(*endpointUrl);
	}
	if (const std::string * model = environmentValue(environment, "OFXIC_MODEL")) {
		settings.modelId = *model;
	}
	if (const std::string * audioUrl = environmentValue(
		environment, "OFXIC_TRANSCRIPTION_ENDPOINT_URL")) {
		settings.transcriptionEndpointUrl = *audioUrl;
	} else if (endpointUrl) {
		settings.transcriptionEndpointUrl = *endpointUrl;
	}
	if (const std::string * protocol = environmentValue(
		environment, "OFXIC_TRANSCRIPTION_AUTORUN")) {
		if (*protocol == "openai") settings.transcriptionProtocol = 0;
		if (*protocol == "whisper-cpp") settings.transcriptionProtocol = 1;
	}
	if (const std::string * model = environmentValue(
		environment, "OFXIC_TRANSCRIPTION_MODEL")) {
		settings.transcriptionModel = *model;
	}
	if (const std::string * segmentationUrl = environmentValue(
		environment, "OFXIC_SEGMENTATION_ENDPOINT_URL")) {
		settings.segmentationEndpointUrl = *segmentationUrl;
	} else if (endpointUrl) {
		settings.segmentationEndpointUrl = *endpointUrl;
	}

	bool mediaBackendOverridden = false;
	if (const std::string * backend = environmentValue(environment, "OFXIC_MEDIA_BACKEND")) {
		if (*backend == "openai") {
			settings.mediaBackend = 0;
			mediaBackendOverridden = true;
		} else if (*backend == "huggingface" || *backend == "hf" || *backend == "fal-ai") {
			settings.mediaBackend = 1;
			mediaBackendOverridden = true;
		} else if (*backend == "stable-diffusion.cpp" || *backend == "sdcpp") {
			settings.mediaBackend = 2;
			mediaBackendOverridden = true;
		}
	} else if (endpointUrl) {
		settings.mediaBackend = endpointUrl->find("huggingface.co") != std::string::npos
			? 1
			: (endpointUrl->find("api.openai.com") != std::string::npos ? 0 : 2);
		mediaBackendOverridden = true;
	}
	if (mediaBackendOverridden) {
		settings.mediaEndpointUrl = settings.mediaBackend == 0
			? "https://api.openai.com/v1"
			: (settings.mediaBackend == 1
				? "https://router.huggingface.co"
				: "http://127.0.0.1:8080");
		if (settings.mediaBackend == 0) settings.mediaImageModel = "gpt-image-2";
		if (settings.mediaBackend == 1) {
			settings.mediaImageModel = "black-forest-labs/FLUX.1-dev";
		}
	}
	if (const std::string * mediaUrl = environmentValue(environment, "OFXIC_MEDIA_ENDPOINT_URL")) {
		settings.mediaEndpointUrl = *mediaUrl;
	}
	if (const std::string * imageModel = environmentValue(environment, "OFXIC_MEDIA_IMAGE_MODEL")) {
		settings.mediaImageModel = *imageModel;
	}
	if (const std::string * videoModel = environmentValue(environment, "OFXIC_MEDIA_VIDEO_MODEL")) {
		settings.mediaVideoModel = *videoModel;
	}
	if (const std::string * kind = environmentValue(environment, "OFXIC_MEDIA_KIND")) {
		if (*kind == "image") settings.mediaKind = 0;
		if (*kind == "video") settings.mediaKind = 1;
	}
	readPositiveOverride(environment, "OFXIC_MEDIA_WIDTH", 8192, settings.mediaWidth);
	readPositiveOverride(environment, "OFXIC_MEDIA_HEIGHT", 8192, settings.mediaHeight);
	readPositiveOverride(environment, "OFXIC_MEDIA_FRAMES", 10000, settings.mediaFrames);
	readPositiveOverride(environment, "OFXIC_MEDIA_FPS", 240, settings.mediaFps);
}

} // namespace ofxICExample
