#include "test_harness.h"
#include "../ofxICExample/src/ExampleMediaContextPolicy.h"

OFXIC_TEST(example_media_context_signature_covers_every_launch_input) {
	ofxICExample::MediaRuntimeConfig base;
	base.serverPath = "runtime/sd-server.exe";
	base.modelPath = "G:/Models/model.gguf";
	base.vaePath = "G:/Models/vae.safetensors";
	base.clipLPath = "clip-l";
	base.clipGPath = "clip-g";
	base.textEncoderPath = "t5";
	base.flashAttention = true;
	const std::string signature = ofxICExample::mediaRuntimeSignature(base);

	auto changed = base;
	changed.vaePath = "other-vae";
	OFXIC_REQUIRE(ofxICExample::mediaRuntimeSignature(changed) != signature);
	changed = base;
	changed.offloadToCpu = true;
	OFXIC_REQUIRE(ofxICExample::mediaRuntimeSignature(changed) != signature);
	changed = base;
	changed.completeCheckpoint = true;
	OFXIC_REQUIRE(ofxICExample::mediaRuntimeSignature(changed) != signature);
}

OFXIC_TEST(example_media_context_matches_model_filenames_portably) {
	OFXIC_REQUIRE(ofxICExample::mediaModelMatches(
		"SD_TURBO.safetensors", "G:/Models/sd_turbo.safetensors"));
	OFXIC_REQUIRE(!ofxICExample::mediaModelMatches(
		"Wan2.1.gguf", "G:/Models/sd_turbo.safetensors"));
}

OFXIC_TEST(example_media_context_reconciles_only_matching_context) {
	ofxIC::MediaCapabilities capabilities;
	capabilities.success = true;
	capabilities.supportedModes = { "vid_gen" };
	capabilities.samplers = { "euler" };
	capabilities.schedulers = { "simple" };
	capabilities.videoOutputFormats = { "avi" };
	ofxICExample::MediaControlSelection selection;
	selection.sampler = "removed";
	selection.scheduler = "simple";
	selection.outputFormat = "webm";
	ofxICExample::reconcileMediaControls(capabilities, true, selection);
	OFXIC_REQUIRE(selection.kind == 1);
	OFXIC_REQUIRE(selection.sampler.empty());
	OFXIC_REQUIRE(selection.scheduler == "simple");
	OFXIC_REQUIRE(selection.outputFormat.empty());
}

OFXIC_TEST(example_media_context_does_not_apply_single_frame_video_default) {
	ofxIC::MediaCapabilities capabilities;
	capabilities.success = true;
	capabilities.defaultWidth = 768;
	capabilities.defaultHeight = 432;
	capabilities.defaultVideoFrames = 1;
	capabilities.defaultFps = 24;
	int width = 512;
	int height = 512;
	int frames = 33;
	int fps = 16;
	ofxICExample::applySafeMediaDefaults(capabilities, width, height, frames, fps);
	OFXIC_REQUIRE(width == 768);
	OFXIC_REQUIRE(height == 432);
	OFXIC_REQUIRE(frames == 33);
	OFXIC_REQUIRE(fps == 24);
}
