#include "ExampleSettings.h"
#include "ExampleAtomicFile.h"
#include "../../src/endpoint/ofxICEndpoint.h"

#include <cstdio>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace ofxICExample {
namespace {

constexpr int settingsVersion = 4;

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

bool parseFloat(const std::string & value, float & destination) {
	std::istringstream stream(value);
	float parsed = 0.0f;
	if (!(stream >> parsed) || !std::isfinite(parsed)) return false;
	stream >> std::ws;
	if (!stream.eof()) return false;
	destination = parsed;
	return true;
}

std::string escapeLineText(const std::string & value) {
	std::string escaped;
	escaped.reserve(value.size());
	for (const char character : value) {
		if (character == '\\') escaped += "\\\\";
		else if (character == '\n') escaped += "\\n";
		else if (character == '\r') escaped += "\\r";
		else escaped += character;
	}
	return escaped;
}

bool parseMultilineString(const std::string & value, std::string & destination) {
	std::string escaped;
	if (!parseString(value, escaped)) return false;
	std::string decoded;
	decoded.reserve(escaped.size());
	for (std::size_t index = 0; index < escaped.size(); ++index) {
		if (escaped[index] != '\\' || index + 1 >= escaped.size()) {
			decoded += escaped[index];
			continue;
		}
		const char next = escaped[++index];
		if (next == 'n') decoded += '\n';
		else if (next == 'r') decoded += '\r';
		else if (next == '\\') decoded += '\\';
		else {
			decoded += '\\';
			decoded += next;
		}
	}
	destination = std::move(decoded);
	return true;
}

bool validText(const std::string & value, std::size_t maximumSize) {
	return value.size() < maximumSize &&
		value.find_first_of("\r\n") == std::string::npos;
}

bool validMultilineText(const std::string & value, std::size_t maximumSize) {
	return value.size() < maximumSize && value.find('\0') == std::string::npos;
}

bool validEndpoint(const std::string & value) {
	return validText(value, 512) && ofxIC::Endpoint::validateBaseUrl(value);
}

bool validSettings(const ExampleSettings & settings) {
	return settings.endpointProfile >= 0 && settings.endpointProfile <= 4 &&
		validEndpoint(settings.endpointUrl) && validText(settings.modelId, 256) &&
		validMultilineText(settings.chatSystemPrompt, 2048) &&
		validMultilineText(settings.chatStopSequences, 1024) &&
		settings.chatMaxTokens >= 1 && settings.chatMaxTokens <= 131072 &&
		std::isfinite(settings.chatTemperature) && settings.chatTemperature >= 0.0f &&
		settings.chatTemperature <= 2.0f && std::isfinite(settings.chatTopP) &&
		settings.chatTopP >= 0.0f && settings.chatTopP <= 1.0f &&
		validText(settings.llamaServerPath, 1024) &&
		validText(settings.llamaModelPath, 1024) &&
		validText(settings.llamaModelDirectory, 1024) &&
		settings.llamaContextSize >= 512 && settings.llamaContextSize <= 1048576 &&
		settings.llamaGpuLayers >= 0 && settings.llamaGpuLayers <= 100000 &&
		settings.llamaFlashAttention >= 0 && settings.llamaFlashAttention <= 1 &&
		validEndpoint(settings.transcriptionEndpointUrl) &&
		settings.transcriptionProtocol >= 0 && settings.transcriptionProtocol <= 1 &&
		validText(settings.transcriptionModel, 256) &&
		validEndpoint(settings.segmentationEndpointUrl) &&
		settings.mediaBackend >= 0 && settings.mediaBackend <= 2 &&
		settings.mediaKind >= 0 && settings.mediaKind <= 1 &&
		validEndpoint(settings.mediaEndpointUrl) &&
		validText(settings.mediaImageModel, 256) &&
		validText(settings.mediaVideoModel, 256) &&
		validText(settings.stableDiffusionModelDirectory, 1024) &&
		validText(settings.stableDiffusionServerPath, 1024) &&
		validText(settings.stableDiffusionModelPath, 1024) &&
		validText(settings.stableDiffusionVaePath, 1024) &&
		validText(settings.stableDiffusionTextEncoderPath, 1024) &&
		validText(settings.stableDiffusionClipLPath, 1024) &&
		validText(settings.stableDiffusionClipGPath, 1024) &&
		settings.stableDiffusionCompleteCheckpoint >= 0 &&
		settings.stableDiffusionCompleteCheckpoint <= 1 &&
		settings.stableDiffusionFlashAttention >= 0 &&
		settings.stableDiffusionFlashAttention <= 1 &&
		settings.stableDiffusionOffloadToCpu >= 0 &&
		settings.stableDiffusionOffloadToCpu <= 1 &&
		settings.mediaWidth >= 1 && settings.mediaWidth <= 8192 &&
		settings.mediaHeight >= 1 && settings.mediaHeight <= 8192 &&
		settings.mediaFrames >= 1 && settings.mediaFrames <= 10000 &&
		settings.mediaFps >= 1 && settings.mediaFps <= 240 &&
		settings.mediaSeed >= -1 && settings.mediaSteps >= 1 && settings.mediaSteps <= 1000 &&
		std::isfinite(settings.mediaGuidance) && settings.mediaGuidance >= 0.0f &&
		settings.mediaGuidance <= 1000.0f && validText(settings.mediaSampler, 64) &&
		validText(settings.mediaScheduler, 64) && validText(settings.mediaOutputFormat, 32) &&
		settings.musicBackend >= 0 && settings.musicBackend <= 1 &&
		validEndpoint(settings.musicEndpointUrl) &&
		settings.musicDuration >= 1 && settings.musicDuration <= 600 &&
		settings.musicOutputFormat >= 0 && settings.musicOutputFormat <= 1 &&
		validText(settings.aceStepServerPath, 1024) &&
		validText(settings.aceStepServerArguments, 2048) &&
		validText(settings.aceStepModelDirectory, 1024) &&
		validText(settings.whisperServerPath, 1024) &&
		validText(settings.whisperModelPath, 1024) &&
		validText(settings.whisperServerArguments, 2048) &&
		validText(settings.samBridgeExecutablePath, 1024) &&
		validText(settings.samBridgeArguments, 2048) &&
		validText(settings.samRunnerPath, 1024) &&
		validText(settings.samModelPath, 1024) &&
		settings.samCuda >= 0 && settings.samCuda <= 1;
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

const char * defaultTranscriptionEndpointUrl(int protocol) {
	return protocol == 1
		? "http://127.0.0.1:8080"
		: "https://api.openai.com/v1";
}

void alignTranscriptionEndpointDefault(ExampleSettings & settings) {
	const int otherProtocol = settings.transcriptionProtocol == 1 ? 0 : 1;
	const std::string current = normalizedUrl(settings.transcriptionEndpointUrl);
	const std::string otherDefault = normalizedUrl(
		defaultTranscriptionEndpointUrl(otherProtocol));
	if (settings.transcriptionEndpointUrl.empty() || current == otherDefault) {
		settings.transcriptionEndpointUrl = defaultTranscriptionEndpointUrl(
			settings.transcriptionProtocol);
	}
}

const char * defaultMusicEndpointUrl(int backend) {
	return backend == 1
		? "https://api.stability.ai"
		: "http://127.0.0.1:8085";
}

void alignMusicEndpointDefault(ExampleSettings & settings) {
	const int otherBackend = settings.musicBackend == 1 ? 0 : 1;
	const std::string current = normalizedUrl(settings.musicEndpointUrl);
	const std::string otherDefault = normalizedUrl(defaultMusicEndpointUrl(otherBackend));
	if (settings.musicEndpointUrl.empty() || current == otherDefault) {
		settings.musicEndpointUrl = defaultMusicEndpointUrl(settings.musicBackend);
	}
}

SettingsLoadStatus loadSettings(const std::string & path, ExampleSettings & settings) {
	std::ifstream input(path, std::ios::binary);
	if (!input) return SettingsLoadStatus::Missing;

	ExampleSettings parsed;
	bool hasVersion = false;
	bool hasMusicBackend = false;
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
			valid = parseInt(value, version) && version >= 1 && version <= settingsVersion;
			hasVersion = valid;
		} else if (key == "endpoint_profile") {
			valid = parseInt(value, parsed.endpointProfile);
		} else if (key == "endpoint_url") {
			valid = parseString(value, parsed.endpointUrl);
		} else if (key == "model_id") {
			valid = parseString(value, parsed.modelId);
		} else if (key == "chat_system_prompt") {
			valid = parseMultilineString(value, parsed.chatSystemPrompt);
		} else if (key == "chat_stop_sequences") {
			valid = parseMultilineString(value, parsed.chatStopSequences);
		} else if (key == "chat_max_output") {
			valid = parseInt(value, parsed.chatMaxTokens);
		} else if (key == "chat_temperature") {
			valid = parseFloat(value, parsed.chatTemperature);
		} else if (key == "chat_top_p") {
			valid = parseFloat(value, parsed.chatTopP);
		} else if (key == "chat_seed") {
			valid = parseInt(value, parsed.chatSeed);
		} else if (key == "llama_server_path") {
			valid = parseString(value, parsed.llamaServerPath);
		} else if (key == "llama_model_path") {
			valid = parseString(value, parsed.llamaModelPath);
		} else if (key == "llama_model_directory") {
			valid = parseString(value, parsed.llamaModelDirectory);
		} else if (key == "llama_context_size") {
			valid = parseInt(value, parsed.llamaContextSize);
		} else if (key == "llama_gpu_layers") {
			valid = parseInt(value, parsed.llamaGpuLayers);
		} else if (key == "llama_flash_attention") {
			valid = parseInt(value, parsed.llamaFlashAttention);
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
		} else if (key == "stable_diffusion_model_directory") {
			valid = parseString(value, parsed.stableDiffusionModelDirectory);
		} else if (key == "stable_diffusion_server_path") {
			valid = parseString(value, parsed.stableDiffusionServerPath);
		} else if (key == "stable_diffusion_model_path") {
			valid = parseString(value, parsed.stableDiffusionModelPath);
		} else if (key == "stable_diffusion_vae_path") {
			valid = parseString(value, parsed.stableDiffusionVaePath);
		} else if (key == "stable_diffusion_text_encoder_path") {
			valid = parseString(value, parsed.stableDiffusionTextEncoderPath);
		} else if (key == "stable_diffusion_clip_l_path") {
			valid = parseString(value, parsed.stableDiffusionClipLPath);
		} else if (key == "stable_diffusion_clip_g_path") {
			valid = parseString(value, parsed.stableDiffusionClipGPath);
		} else if (key == "stable_diffusion_complete_checkpoint") {
			valid = parseInt(value, parsed.stableDiffusionCompleteCheckpoint);
		} else if (key == "stable_diffusion_flash_attention") {
			valid = parseInt(value, parsed.stableDiffusionFlashAttention);
		} else if (key == "stable_diffusion_offload_to_cpu") {
			valid = parseInt(value, parsed.stableDiffusionOffloadToCpu);
		} else if (key == "media_width") {
			valid = parseInt(value, parsed.mediaWidth);
		} else if (key == "media_height") {
			valid = parseInt(value, parsed.mediaHeight);
		} else if (key == "media_frames") {
			valid = parseInt(value, parsed.mediaFrames);
		} else if (key == "media_fps") {
			valid = parseInt(value, parsed.mediaFps);
		} else if (key == "media_seed") {
			valid = parseInt(value, parsed.mediaSeed);
		} else if (key == "media_steps") {
			valid = parseInt(value, parsed.mediaSteps);
		} else if (key == "media_guidance") {
			valid = parseFloat(value, parsed.mediaGuidance);
		} else if (key == "media_sampler") {
			valid = parseString(value, parsed.mediaSampler);
		} else if (key == "media_scheduler") {
			valid = parseString(value, parsed.mediaScheduler);
		} else if (key == "media_output_format") {
			valid = parseString(value, parsed.mediaOutputFormat);
		} else if (key == "music_backend") {
			valid = parseInt(value, parsed.musicBackend);
			hasMusicBackend = valid;
		} else if (key == "music_endpoint_url") {
			valid = parseString(value, parsed.musicEndpointUrl);
		} else if (key == "music_duration") {
			valid = parseInt(value, parsed.musicDuration);
		} else if (key == "music_output_format") {
			valid = parseInt(value, parsed.musicOutputFormat);
		} else if (key == "ace_step_server_path") {
			valid = parseString(value, parsed.aceStepServerPath);
		} else if (key == "ace_step_server_arguments") {
			valid = parseString(value, parsed.aceStepServerArguments);
		} else if (key == "ace_step_model_directory") {
			valid = parseString(value, parsed.aceStepModelDirectory);
		} else if (key == "whisper_server_path") {
			valid = parseString(value, parsed.whisperServerPath);
		} else if (key == "whisper_model_path") {
			valid = parseString(value, parsed.whisperModelPath);
		} else if (key == "whisper_server_arguments") {
			valid = parseString(value, parsed.whisperServerArguments);
		} else if (key == "sam_bridge_executable_path") {
			valid = parseString(value, parsed.samBridgeExecutablePath);
		} else if (key == "sam_bridge_arguments") {
			valid = parseString(value, parsed.samBridgeArguments);
		} else if (key == "sam_runner_path") {
			valid = parseString(value, parsed.samRunnerPath);
		} else if (key == "sam_model_path") {
			valid = parseString(value, parsed.samModelPath);
		} else if (key == "sam_cuda") {
			valid = parseInt(value, parsed.samCuda);
		}
		if (!valid) return SettingsLoadStatus::Invalid;
	}
	if (!input.eof() || !hasVersion || !validSettings(parsed)) {
		return SettingsLoadStatus::Invalid;
	}
	alignTranscriptionEndpointDefault(parsed);
	if (!hasMusicBackend) {
		parsed.musicBackend = normalizedUrl(parsed.musicEndpointUrl) ==
			normalizedUrl(defaultMusicEndpointUrl(1)) ? 1 : 0;
	} else {
		alignMusicEndpointDefault(parsed);
	}
	settings = std::move(parsed);
	return SettingsLoadStatus::Loaded;
}

bool saveSettings(const std::string & path, const ExampleSettings & settings) {
	if (path.empty() || !validSettings(settings)) return false;
	std::ostringstream output;
	output << "version=" << settingsVersion << '\n'
		<< "endpoint_profile=" << settings.endpointProfile << '\n'
		<< "endpoint_url=" << std::quoted(settings.endpointUrl) << '\n'
		<< "model_id=" << std::quoted(settings.modelId) << '\n'
		<< "chat_system_prompt=" << std::quoted(escapeLineText(settings.chatSystemPrompt)) << '\n'
		<< "chat_stop_sequences=" << std::quoted(escapeLineText(settings.chatStopSequences)) << '\n'
		<< "chat_max_output=" << settings.chatMaxTokens << '\n'
		<< "chat_temperature=" << settings.chatTemperature << '\n'
		<< "chat_top_p=" << settings.chatTopP << '\n'
		<< "chat_seed=" << settings.chatSeed << '\n'
		<< "llama_server_path=" << std::quoted(settings.llamaServerPath) << '\n'
		<< "llama_model_path=" << std::quoted(settings.llamaModelPath) << '\n'
		<< "llama_model_directory=" << std::quoted(settings.llamaModelDirectory) << '\n'
		<< "llama_context_size=" << settings.llamaContextSize << '\n'
		<< "llama_gpu_layers=" << settings.llamaGpuLayers << '\n'
		<< "llama_flash_attention=" << settings.llamaFlashAttention << '\n'
		<< "transcription_endpoint_url=" << std::quoted(settings.transcriptionEndpointUrl) << '\n'
		<< "transcription_protocol=" << settings.transcriptionProtocol << '\n'
		<< "transcription_model=" << std::quoted(settings.transcriptionModel) << '\n'
		<< "segmentation_endpoint_url=" << std::quoted(settings.segmentationEndpointUrl) << '\n'
		<< "media_backend=" << settings.mediaBackend << '\n'
		<< "media_kind=" << settings.mediaKind << '\n'
		<< "media_endpoint_url=" << std::quoted(settings.mediaEndpointUrl) << '\n'
		<< "media_image_model=" << std::quoted(settings.mediaImageModel) << '\n'
		<< "media_video_model=" << std::quoted(settings.mediaVideoModel) << '\n'
		<< "stable_diffusion_model_directory=" << std::quoted(settings.stableDiffusionModelDirectory) << '\n'
		<< "stable_diffusion_server_path=" << std::quoted(settings.stableDiffusionServerPath) << '\n'
		<< "stable_diffusion_model_path=" << std::quoted(settings.stableDiffusionModelPath) << '\n'
		<< "stable_diffusion_vae_path=" << std::quoted(settings.stableDiffusionVaePath) << '\n'
		<< "stable_diffusion_text_encoder_path=" << std::quoted(settings.stableDiffusionTextEncoderPath) << '\n'
		<< "stable_diffusion_clip_l_path=" << std::quoted(settings.stableDiffusionClipLPath) << '\n'
		<< "stable_diffusion_clip_g_path=" << std::quoted(settings.stableDiffusionClipGPath) << '\n'
		<< "stable_diffusion_complete_checkpoint=" << settings.stableDiffusionCompleteCheckpoint << '\n'
		<< "stable_diffusion_flash_attention=" << settings.stableDiffusionFlashAttention << '\n'
		<< "stable_diffusion_offload_to_cpu=" << settings.stableDiffusionOffloadToCpu << '\n'
		<< "media_width=" << settings.mediaWidth << '\n'
		<< "media_height=" << settings.mediaHeight << '\n'
		<< "media_frames=" << settings.mediaFrames << '\n'
		<< "media_fps=" << settings.mediaFps << '\n'
		<< "media_seed=" << settings.mediaSeed << '\n'
		<< "media_steps=" << settings.mediaSteps << '\n'
		<< "media_guidance=" << settings.mediaGuidance << '\n'
		<< "media_sampler=" << std::quoted(settings.mediaSampler) << '\n'
		<< "media_scheduler=" << std::quoted(settings.mediaScheduler) << '\n'
		<< "media_output_format=" << std::quoted(settings.mediaOutputFormat) << '\n'
		<< "music_backend=" << settings.musicBackend << '\n'
		<< "music_endpoint_url=" << std::quoted(settings.musicEndpointUrl) << '\n'
		<< "music_duration=" << settings.musicDuration << '\n'
		<< "music_output_format=" << settings.musicOutputFormat << '\n'
		<< "ace_step_server_path=" << std::quoted(settings.aceStepServerPath) << '\n'
		<< "ace_step_server_arguments=" << std::quoted(settings.aceStepServerArguments) << '\n'
		<< "ace_step_model_directory=" << std::quoted(settings.aceStepModelDirectory) << '\n'
		<< "whisper_server_path=" << std::quoted(settings.whisperServerPath) << '\n'
		<< "whisper_model_path=" << std::quoted(settings.whisperModelPath) << '\n'
		<< "whisper_server_arguments=" << std::quoted(settings.whisperServerArguments) << '\n'
		<< "sam_bridge_executable_path=" << std::quoted(settings.samBridgeExecutablePath) << '\n'
		<< "sam_bridge_arguments=" << std::quoted(settings.samBridgeArguments) << '\n'
		<< "sam_runner_path=" << std::quoted(settings.samRunnerPath) << '\n'
		<< "sam_model_path=" << std::quoted(settings.samModelPath) << '\n'
		<< "sam_cuda=" << settings.samCuda << '\n';
	return output && writeTextFileAtomically(path, output.str());
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
	const std::string * audioUrl = environmentValue(
		environment, "OFXIC_TRANSCRIPTION_ENDPOINT_URL");
	if (audioUrl) {
		settings.transcriptionEndpointUrl = *audioUrl;
	} else if (endpointUrl) {
		settings.transcriptionEndpointUrl = *endpointUrl;
	}
	bool transcriptionProtocolOverridden = false;
	if (const std::string * protocol = environmentValue(
		environment, "OFXIC_TRANSCRIPTION_AUTORUN")) {
		if (*protocol == "openai") {
			settings.transcriptionProtocol = 0;
			transcriptionProtocolOverridden = true;
		}
		if (*protocol == "whisper-cpp") {
			settings.transcriptionProtocol = 1;
			transcriptionProtocolOverridden = true;
		}
	}
	if (transcriptionProtocolOverridden && !audioUrl && !endpointUrl) {
		alignTranscriptionEndpointDefault(settings);
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
				: "http://127.0.0.1:8081");
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
	if (const std::string * seed = environmentValue(environment, "OFXIC_MEDIA_SEED")) {
		int parsed = 0;
		if (parseInt(*seed, parsed) && parsed >= -1) settings.mediaSeed = parsed;
	}
	readPositiveOverride(environment, "OFXIC_MEDIA_STEPS", 1000, settings.mediaSteps);
	if (const std::string * guidance = environmentValue(environment, "OFXIC_MEDIA_GUIDANCE")) {
		float parsed = 0.0f;
		if (parseFloat(*guidance, parsed) && parsed >= 0.0f && parsed <= 1000.0f)
			settings.mediaGuidance = parsed;
	}
	if (const std::string * sampler = environmentValue(environment, "OFXIC_MEDIA_SAMPLER"))
		if (validText(*sampler, 64)) settings.mediaSampler = *sampler;
	if (const std::string * scheduler = environmentValue(environment, "OFXIC_MEDIA_SCHEDULER"))
		if (validText(*scheduler, 64)) settings.mediaScheduler = *scheduler;
	if (const std::string * format = environmentValue(environment, "OFXIC_MEDIA_OUTPUT_FORMAT"))
		if (validText(*format, 32)) settings.mediaOutputFormat = *format;
	if (const std::string * musicUrl = environmentValue(
		environment, "OFXIC_MUSIC_ENDPOINT_URL")) {
		settings.musicEndpointUrl = *musicUrl;
	}
	if (const std::string * backend = environmentValue(
		environment, "OFXIC_MUSIC_BACKEND")) {
		const int previousBackend = settings.musicBackend;
		if (*backend == "acestep" || *backend == "local") settings.musicBackend = 0;
		if (*backend == "stability" || *backend == "stability-ai") settings.musicBackend = 1;
		if (!environmentValue(environment, "OFXIC_MUSIC_ENDPOINT_URL") &&
			normalizedUrl(settings.musicEndpointUrl) ==
				normalizedUrl(defaultMusicEndpointUrl(previousBackend))) {
			settings.musicEndpointUrl = defaultMusicEndpointUrl(settings.musicBackend);
		}
	}
	readPositiveOverride(environment, "OFXIC_MUSIC_DURATION", 600, settings.musicDuration);
	if (const std::string * format = environmentValue(
		environment, "OFXIC_MUSIC_OUTPUT_FORMAT")) {
		if (*format == "mp3") settings.musicOutputFormat = 0;
		if (*format == "wav") settings.musicOutputFormat = 1;
	}
}

} // namespace ofxICExample
