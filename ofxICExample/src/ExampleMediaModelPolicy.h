#pragma once

#include <optional>
#include <string>

namespace ofxICExample {

enum class MediaModelKind {
	Image,
	Video
};

// Infers only well-known model families. Unknown names deliberately remain
// user-selectable because stable-diffusion.cpp gains model support frequently.
std::optional<MediaModelKind> inferMediaModelKind(const std::string & path);

} // namespace ofxICExample
