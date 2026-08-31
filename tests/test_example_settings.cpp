#include "test_harness.h"
#include "../ofxICExample/src/ExampleSettings.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

namespace {

const std::string settingsPath = "ofxic-example-settings-fixture.cfg";

} // namespace

OFXIC_TEST(example_settings_round_trip_non_secret_values) {
	ofxICExample::ExampleSettings saved;
	saved.endpointProfile = 4;
	saved.endpointUrl = "https://example.test/v1/custom";
	saved.modelId = "org/model with spaces";
	saved.chatSystemPrompt = "Answer briefly.\nCite sources.";
	saved.chatStopSequences = "STOP\nEND\\MARKER";
	saved.chatMaxTokens = 2048;
	saved.chatTemperature = 0.25f;
	saved.chatTopP = 0.8f;
	saved.chatSeed = 42;
	saved.llamaServerPath = "C:/runtimes/llama-server.exe";
	saved.llamaModelPath = "G:/Models/chat.gguf";
	saved.llamaModelDirectory = "G:/Models";
	saved.llamaContextSize = 8192;
	saved.llamaGpuLayers = 120;
	saved.llamaFlashAttention = 0;
	saved.transcriptionEndpointUrl = "https://audio.example.test";
	saved.transcriptionProtocol = 1;
	saved.transcriptionModel = "org/audio-model";
	saved.segmentationEndpointUrl = "http://127.0.0.1:19001";
	saved.mediaBackend = 1;
	saved.mediaKind = 1;
	saved.mediaEndpointUrl = "https://media.example.test";
	saved.mediaImageModel = "org/image-model";
	saved.mediaVideoModel = "org/video-model";
	saved.stableDiffusionServerPath = "C:/runtimes/sd-server.exe";
	saved.stableDiffusionModelDirectory = "G:/Models/diffusion";
	saved.stableDiffusionModelPath = "G:/Models/diffusion/model.safetensors";
	saved.stableDiffusionVaePath = "G:/Models/diffusion/vae.safetensors";
	saved.stableDiffusionTextEncoderPath = "G:/Models/diffusion/t5.gguf";
	saved.stableDiffusionClipLPath = "G:/Models/diffusion/clip-l.safetensors";
	saved.stableDiffusionClipGPath = "G:/Models/diffusion/clip-g.safetensors";
	saved.stableDiffusionCompleteCheckpoint = 1;
	saved.stableDiffusionFlashAttention = 0;
	saved.stableDiffusionOffloadToCpu = 1;
	saved.mediaWidth = 768;
	saved.mediaHeight = 432;
	saved.mediaFrames = 49;
	saved.mediaFps = 24;
	saved.musicBackend = 1;
	saved.musicEndpointUrl = "https://music.example.test";
	saved.musicDuration = 45;
	saved.musicOutputFormat = 1;
	saved.aceStepServerPath = "C:/runtimes/acestep.exe";
	saved.aceStepServerArguments = "--port 8085";
	saved.aceStepModelDirectory = "G:/Models";
	saved.whisperServerPath = "C:/runtimes/whisper-server.exe";
	saved.whisperModelPath = "G:/Models/whisper.bin";
	saved.whisperServerArguments = "--port 8082";
	saved.samBridgeExecutablePath = "C:/Python/python.exe";
	saved.samBridgeArguments = "sam-bridge-server.py --port 18085";
	saved.samRunnerPath = "C:/runtimes/sam-python-runner.py";
	saved.samModelPath = "G:/Models/sam_vit_b_01ec64.pth";
	saved.samCuda = 0;

	OFXIC_REQUIRE(ofxICExample::saveSettings(settingsPath, saved));
	ofxICExample::ExampleSettings loaded;
	OFXIC_REQUIRE(
		ofxICExample::loadSettings(settingsPath, loaded) ==
		ofxICExample::SettingsLoadStatus::Loaded);
	OFXIC_REQUIRE(loaded.endpointProfile == saved.endpointProfile);
	OFXIC_REQUIRE(loaded.endpointUrl == saved.endpointUrl);
	OFXIC_REQUIRE(loaded.modelId == saved.modelId);
	OFXIC_REQUIRE(loaded.chatSystemPrompt == saved.chatSystemPrompt);
	OFXIC_REQUIRE(loaded.chatStopSequences == saved.chatStopSequences);
	OFXIC_REQUIRE(loaded.chatMaxTokens == saved.chatMaxTokens);
	OFXIC_REQUIRE(loaded.chatTemperature == saved.chatTemperature);
	OFXIC_REQUIRE(loaded.chatTopP == saved.chatTopP);
	OFXIC_REQUIRE(loaded.chatSeed == saved.chatSeed);
	OFXIC_REQUIRE(loaded.llamaServerPath == saved.llamaServerPath);
	OFXIC_REQUIRE(loaded.llamaModelPath == saved.llamaModelPath);
	OFXIC_REQUIRE(loaded.llamaModelDirectory == saved.llamaModelDirectory);
	OFXIC_REQUIRE(loaded.llamaContextSize == saved.llamaContextSize);
	OFXIC_REQUIRE(loaded.llamaGpuLayers == saved.llamaGpuLayers);
	OFXIC_REQUIRE(loaded.llamaFlashAttention == saved.llamaFlashAttention);
	OFXIC_REQUIRE(loaded.transcriptionEndpointUrl == saved.transcriptionEndpointUrl);
	OFXIC_REQUIRE(loaded.transcriptionProtocol == saved.transcriptionProtocol);
	OFXIC_REQUIRE(loaded.transcriptionModel == saved.transcriptionModel);
	OFXIC_REQUIRE(loaded.segmentationEndpointUrl == saved.segmentationEndpointUrl);
	OFXIC_REQUIRE(loaded.mediaBackend == saved.mediaBackend);
	OFXIC_REQUIRE(loaded.mediaKind == saved.mediaKind);
	OFXIC_REQUIRE(loaded.mediaEndpointUrl == saved.mediaEndpointUrl);
	OFXIC_REQUIRE(loaded.mediaImageModel == saved.mediaImageModel);
	OFXIC_REQUIRE(loaded.mediaVideoModel == saved.mediaVideoModel);
	OFXIC_REQUIRE(loaded.stableDiffusionServerPath == saved.stableDiffusionServerPath);
	OFXIC_REQUIRE(loaded.stableDiffusionModelDirectory == saved.stableDiffusionModelDirectory);
	OFXIC_REQUIRE(loaded.stableDiffusionModelPath == saved.stableDiffusionModelPath);
	OFXIC_REQUIRE(loaded.stableDiffusionVaePath == saved.stableDiffusionVaePath);
	OFXIC_REQUIRE(loaded.stableDiffusionTextEncoderPath == saved.stableDiffusionTextEncoderPath);
	OFXIC_REQUIRE(loaded.stableDiffusionClipLPath == saved.stableDiffusionClipLPath);
	OFXIC_REQUIRE(loaded.stableDiffusionClipGPath == saved.stableDiffusionClipGPath);
	OFXIC_REQUIRE(loaded.stableDiffusionCompleteCheckpoint == saved.stableDiffusionCompleteCheckpoint);
	OFXIC_REQUIRE(loaded.stableDiffusionFlashAttention == saved.stableDiffusionFlashAttention);
	OFXIC_REQUIRE(loaded.stableDiffusionOffloadToCpu == saved.stableDiffusionOffloadToCpu);
	OFXIC_REQUIRE(loaded.mediaWidth == saved.mediaWidth);
	OFXIC_REQUIRE(loaded.mediaHeight == saved.mediaHeight);
	OFXIC_REQUIRE(loaded.mediaFrames == saved.mediaFrames);
	OFXIC_REQUIRE(loaded.mediaFps == saved.mediaFps);
	OFXIC_REQUIRE(loaded.musicBackend == saved.musicBackend);
	OFXIC_REQUIRE(loaded.musicEndpointUrl == saved.musicEndpointUrl);
	OFXIC_REQUIRE(loaded.musicDuration == saved.musicDuration);
	OFXIC_REQUIRE(loaded.musicOutputFormat == saved.musicOutputFormat);
	OFXIC_REQUIRE(loaded.aceStepServerPath == saved.aceStepServerPath);
	OFXIC_REQUIRE(loaded.aceStepServerArguments == saved.aceStepServerArguments);
	OFXIC_REQUIRE(loaded.aceStepModelDirectory == saved.aceStepModelDirectory);
	OFXIC_REQUIRE(loaded.whisperServerPath == saved.whisperServerPath);
	OFXIC_REQUIRE(loaded.whisperModelPath == saved.whisperModelPath);
	OFXIC_REQUIRE(loaded.whisperServerArguments == saved.whisperServerArguments);
	OFXIC_REQUIRE(loaded.samBridgeExecutablePath == saved.samBridgeExecutablePath);
	OFXIC_REQUIRE(loaded.samBridgeArguments == saved.samBridgeArguments);
	OFXIC_REQUIRE(loaded.samRunnerPath == saved.samRunnerPath);
	OFXIC_REQUIRE(loaded.samModelPath == saved.samModelPath);
	OFXIC_REQUIRE(loaded.samCuda == saved.samCuda);

	std::ifstream input(settingsPath, std::ios::binary);
	const std::string serialized(
		(std::istreambuf_iterator<char>(input)),
		std::istreambuf_iterator<char>());
	OFXIC_REQUIRE(serialized.find("token") == std::string::npos);
	OFXIC_REQUIRE(serialized.find("api_key") == std::string::npos);
	input.close();
	OFXIC_REQUIRE(ofxICExample::removeSettings(settingsPath));
}

OFXIC_TEST(example_settings_reject_corruption_without_changing_defaults) {
	{
		std::ofstream output(settingsPath, std::ios::binary | std::ios::trunc);
		output << "version=1\nmedia_fps=0\n";
	}
	ofxICExample::ExampleSettings settings;
	settings.modelId = "safe-default";
	OFXIC_REQUIRE(
		ofxICExample::loadSettings(settingsPath, settings) ==
		ofxICExample::SettingsLoadStatus::Invalid);
	OFXIC_REQUIRE(settings.modelId == "safe-default");
	OFXIC_REQUIRE(settings.mediaFps == 16);
	OFXIC_REQUIRE(ofxICExample::removeSettings(settingsPath));
	OFXIC_REQUIRE(
		ofxICExample::loadSettings(settingsPath, settings) ==
		ofxICExample::SettingsLoadStatus::Missing);
}

OFXIC_TEST(example_settings_loads_version_one_without_audio_keys) {
	{
		std::ofstream output(settingsPath, std::ios::binary | std::ios::trunc);
		output << "version=1\nendpoint_profile=1\nmodel_id=\"legacy-model\"\n";
	}
	ofxICExample::ExampleSettings settings;
	OFXIC_REQUIRE(
		ofxICExample::loadSettings(settingsPath, settings) ==
		ofxICExample::SettingsLoadStatus::Loaded);
	OFXIC_REQUIRE(settings.endpointProfile == 1);
	OFXIC_REQUIRE(settings.modelId == "legacy-model");
	OFXIC_REQUIRE(settings.transcriptionProtocol == 0);
	OFXIC_REQUIRE(settings.transcriptionModel == "whisper-1");
	OFXIC_REQUIRE(settings.transcriptionEndpointUrl == "https://api.openai.com/v1");
	OFXIC_REQUIRE(settings.segmentationEndpointUrl == "http://127.0.0.1:18085");
	OFXIC_REQUIRE(settings.musicBackend == 0);
	OFXIC_REQUIRE(settings.musicEndpointUrl == "http://127.0.0.1:8085");
	OFXIC_REQUIRE(settings.musicDuration == 30);
	OFXIC_REQUIRE(settings.musicOutputFormat == 0);
	OFXIC_REQUIRE(ofxICExample::removeSettings(settingsPath));
}

OFXIC_TEST(example_music_environment_overrides_are_provider_specific) {
	ofxICExample::ExampleSettings settings;
	ofxICExample::applyEnvironmentOverrides(settings, {
		{ "OFXIC_MUSIC_BACKEND", "stability" },
		{ "OFXIC_MUSIC_ENDPOINT_URL", "https://music.example.test" },
		{ "OFXIC_MUSIC_DURATION", "75" },
		{ "OFXIC_MUSIC_OUTPUT_FORMAT", "wav" },
	});
	OFXIC_REQUIRE(settings.musicBackend == 1);
	OFXIC_REQUIRE(settings.musicEndpointUrl == "https://music.example.test");
	OFXIC_REQUIRE(settings.musicDuration == 75);
	OFXIC_REQUIRE(settings.musicOutputFormat == 1);
}

OFXIC_TEST(example_music_backend_selects_its_default_endpoint) {
	ofxICExample::ExampleSettings settings;
	OFXIC_REQUIRE(settings.musicBackend == 0);
	OFXIC_REQUIRE(settings.musicEndpointUrl == "http://127.0.0.1:8085");

	settings.musicBackend = 1;
	ofxICExample::alignMusicEndpointDefault(settings);
	OFXIC_REQUIRE(settings.musicEndpointUrl == "https://api.stability.ai");

	settings.musicBackend = 0;
	ofxICExample::alignMusicEndpointDefault(settings);
	OFXIC_REQUIRE(settings.musicEndpointUrl == "http://127.0.0.1:8085");

	settings.musicEndpointUrl = "http://music-gateway.example.test";
	settings.musicBackend = 1;
	ofxICExample::alignMusicEndpointDefault(settings);
	OFXIC_REQUIRE(settings.musicEndpointUrl == "http://music-gateway.example.test");
}

OFXIC_TEST(example_settings_infers_stability_for_legacy_music_url) {
	{
		std::ofstream output(settingsPath, std::ios::binary | std::ios::trunc);
		output << "version=1\n"
			<< "music_endpoint_url=\"https://api.stability.ai\"\n";
	}
	ofxICExample::ExampleSettings settings;
	OFXIC_REQUIRE(
		ofxICExample::loadSettings(settingsPath, settings) ==
		ofxICExample::SettingsLoadStatus::Loaded);
	OFXIC_REQUIRE(settings.musicBackend == 1);
	OFXIC_REQUIRE(settings.musicEndpointUrl == "https://api.stability.ai");
	OFXIC_REQUIRE(ofxICExample::removeSettings(settingsPath));
}

OFXIC_TEST(example_transcription_protocol_defaults_remain_aligned) {
	ofxICExample::ExampleSettings settings;
	OFXIC_REQUIRE(settings.transcriptionProtocol == 0);
	OFXIC_REQUIRE(settings.transcriptionEndpointUrl == "https://api.openai.com/v1");

	settings.transcriptionEndpointUrl = "http://127.0.0.1:8080";
	ofxICExample::alignTranscriptionEndpointDefault(settings);
	OFXIC_REQUIRE(settings.transcriptionEndpointUrl == "https://api.openai.com/v1");

	settings.transcriptionProtocol = 1;
	ofxICExample::alignTranscriptionEndpointDefault(settings);
	OFXIC_REQUIRE(settings.transcriptionEndpointUrl == "http://127.0.0.1:8080");

	settings.transcriptionProtocol = 0;
	settings.transcriptionEndpointUrl = "https://audio-gateway.example.test/v1";
	ofxICExample::alignTranscriptionEndpointDefault(settings);
	OFXIC_REQUIRE(
		settings.transcriptionEndpointUrl == "https://audio-gateway.example.test/v1");
}

OFXIC_TEST(example_settings_migrates_the_stale_openai_audio_default) {
	{
		std::ofstream output(settingsPath, std::ios::binary | std::ios::trunc);
		output << "version=1\n"
			<< "transcription_endpoint_url=\"http://127.0.0.1:8080\"\n"
			<< "transcription_protocol=0\n";
	}
	ofxICExample::ExampleSettings settings;
	OFXIC_REQUIRE(
		ofxICExample::loadSettings(settingsPath, settings) ==
		ofxICExample::SettingsLoadStatus::Loaded);
	OFXIC_REQUIRE(settings.transcriptionProtocol == 0);
	OFXIC_REQUIRE(settings.transcriptionEndpointUrl == "https://api.openai.com/v1");
	OFXIC_REQUIRE(ofxICExample::removeSettings(settingsPath));
}

OFXIC_TEST(example_transcription_protocol_environment_selects_its_default) {
	ofxICExample::ExampleSettings settings;
	settings.transcriptionProtocol = 1;
	settings.transcriptionEndpointUrl = "http://127.0.0.1:8080";
	ofxICExample::applyEnvironmentOverrides(settings, {
		{ "OFXIC_TRANSCRIPTION_AUTORUN", "openai" },
	});
	OFXIC_REQUIRE(settings.transcriptionProtocol == 0);
	OFXIC_REQUIRE(settings.transcriptionEndpointUrl == "https://api.openai.com/v1");

	settings.transcriptionEndpointUrl = "http://127.0.0.1:8080";
	ofxICExample::applyEnvironmentOverrides(settings, {
		{ "OFXIC_TRANSCRIPTION_AUTORUN", "openai" },
		{ "OFXIC_TRANSCRIPTION_ENDPOINT_URL", "http://127.0.0.1:8080" },
	});
	OFXIC_REQUIRE(settings.transcriptionEndpointUrl == "http://127.0.0.1:8080");
}

OFXIC_TEST(example_environment_overrides_saved_settings_predictably) {
	ofxICExample::ExampleSettings settings;
	settings.endpointProfile = 1;
	settings.endpointUrl = "http://127.0.0.1:1234";
	settings.modelId = "stored-model";
	settings.transcriptionProtocol = 0;
	settings.transcriptionModel = "stored-audio-model";
	settings.mediaBackend = 2;
	settings.mediaEndpointUrl = "http://stored-media.test";
	settings.mediaHeight = 720;

	const std::map<std::string, std::string> environment{
		{ "OFXIC_API_KEY", "must-never-be-persisted" },
		{ "OFXIC_ENDPOINT_URL", "https://api.openai.com/v1" },
		{ "OFXIC_MODEL", "environment-model" },
		{ "OFXIC_TRANSCRIPTION_AUTORUN", "whisper-cpp" },
		{ "OFXIC_TRANSCRIPTION_ENDPOINT_URL", "http://127.0.0.1:19002" },
		{ "OFXIC_TRANSCRIPTION_MODEL", "environment-audio-model" },
		{ "OFXIC_SEGMENTATION_ENDPOINT_URL", "http://127.0.0.1:19003" },
		{ "OFXIC_MEDIA_BACKEND", "hf" },
		{ "OFXIC_MEDIA_ENDPOINT_URL", "https://media.environment.test" },
		{ "OFXIC_MEDIA_VIDEO_MODEL", "environment-video" },
		{ "OFXIC_MEDIA_KIND", "video" },
		{ "OFXIC_MEDIA_WIDTH", "1024" },
		{ "OFXIC_MEDIA_HEIGHT", "invalid" },
		{ "OFXIC_MEDIA_FRAMES", "65" },
		{ "OFXIC_MEDIA_FPS", "30" },
	};
	ofxICExample::applyEnvironmentOverrides(settings, environment);

	OFXIC_REQUIRE(settings.endpointProfile == 3);
	OFXIC_REQUIRE(settings.endpointUrl == "https://api.openai.com/v1");
	OFXIC_REQUIRE(settings.modelId == "environment-model");
	OFXIC_REQUIRE(settings.transcriptionProtocol == 1);
	OFXIC_REQUIRE(settings.transcriptionEndpointUrl == "http://127.0.0.1:19002");
	OFXIC_REQUIRE(settings.transcriptionModel == "environment-audio-model");
	OFXIC_REQUIRE(settings.segmentationEndpointUrl == "http://127.0.0.1:19003");
	OFXIC_REQUIRE(settings.mediaBackend == 1);
	OFXIC_REQUIRE(settings.mediaEndpointUrl == "https://media.environment.test");
	OFXIC_REQUIRE(settings.mediaImageModel == "black-forest-labs/FLUX.1-dev");
	OFXIC_REQUIRE(settings.mediaVideoModel == "environment-video");
	OFXIC_REQUIRE(settings.mediaKind == 1);
	OFXIC_REQUIRE(settings.mediaWidth == 1024);
	OFXIC_REQUIRE(settings.mediaHeight == 720);
	OFXIC_REQUIRE(settings.mediaFrames == 65);
	OFXIC_REQUIRE(settings.mediaFps == 30);
	OFXIC_REQUIRE(ofxICExample::saveSettings(settingsPath, settings));
	std::ifstream input(settingsPath, std::ios::binary);
	const std::string serialized(
		(std::istreambuf_iterator<char>(input)),
		std::istreambuf_iterator<char>());
	OFXIC_REQUIRE(serialized.find("must-never-be-persisted") == std::string::npos);
	input.close();
	OFXIC_REQUIRE(ofxICExample::removeSettings(settingsPath));
}

OFXIC_TEST(example_sdcpp_environment_backend_uses_media_server_port) {
	ofxICExample::ExampleSettings settings;
	settings.mediaEndpointUrl = "http://stored-media.test";
	ofxICExample::applyEnvironmentOverrides(settings, {
		{ "OFXIC_MEDIA_BACKEND", "sdcpp" },
	});
	OFXIC_REQUIRE(settings.mediaBackend == 2);
	OFXIC_REQUIRE(settings.mediaEndpointUrl == "http://127.0.0.1:8081");
}

OFXIC_TEST(example_general_endpoint_override_remains_a_legacy_task_default) {
	ofxICExample::ExampleSettings settings;
	settings.transcriptionEndpointUrl = "http://saved-audio.test";
	settings.segmentationEndpointUrl = "http://saved-sam.test";
	const std::map<std::string, std::string> environment{
		{ "OFXIC_ENDPOINT_URL", "http://legacy-automation.test" },
	};
	ofxICExample::applyEnvironmentOverrides(settings, environment);
	OFXIC_REQUIRE(settings.transcriptionEndpointUrl == "http://legacy-automation.test");
	OFXIC_REQUIRE(settings.segmentationEndpointUrl == "http://legacy-automation.test");
}
