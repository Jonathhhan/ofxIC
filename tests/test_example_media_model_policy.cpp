#include "test_harness.h"
#include "../ofxICExample/src/ExampleMediaModelPolicy.h"

OFXIC_TEST(example_media_policy_recognizes_sd_turbo_as_image) {
	const auto kind = ofxICExample::inferMediaModelKind(
		"G:/Models/sd_turbo.safetensors");
	OFXIC_REQUIRE(kind.has_value());
	OFXIC_REQUIRE(*kind == ofxICExample::MediaModelKind::Image);
}

OFXIC_TEST(example_media_policy_recognizes_wan_as_video) {
	const auto kind = ofxICExample::inferMediaModelKind(
		"G:/Models/Wan2.2-TI2V-5B-Q6_K.gguf");
	OFXIC_REQUIRE(kind.has_value());
	OFXIC_REQUIRE(*kind == ofxICExample::MediaModelKind::Video);
}

OFXIC_TEST(example_media_policy_leaves_unknown_models_user_selectable) {
	OFXIC_REQUIRE(!ofxICExample::inferMediaModelKind(
		"G:/Models/future-model.gguf").has_value());
}
