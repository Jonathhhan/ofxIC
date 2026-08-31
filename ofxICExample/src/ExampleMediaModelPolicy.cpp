#include "ExampleMediaModelPolicy.h"

#include <algorithm>
#include <cctype>

namespace ofxICExample {
namespace {

bool containsAny(const std::string & value,
	std::initializer_list<const char *> terms) {
	for (const char * term : terms)
		if (value.find(term) != std::string::npos) return true;
	return false;
}

} // namespace

std::optional<MediaModelKind> inferMediaModelKind(const std::string & path) {
	std::string name = path;
	std::transform(name.begin(), name.end(), name.begin(), [](unsigned char value) {
		return static_cast<char>(std::tolower(value));
	});
	if (containsAny(name, { "wan", "ltx-video", "ltxv", "hunyuanvideo",
		"hunyuan-video", "animatediff", "cogvideox", "mochi" }))
		return MediaModelKind::Video;
	if (containsAny(name, { "sd_turbo", "sd-turbo", "stable-diffusion",
		"stable_diffusion", "sdxl", "flux", "sd3", "dreamshaper" }))
		return MediaModelKind::Image;
	return std::nullopt;
}

} // namespace ofxICExample
