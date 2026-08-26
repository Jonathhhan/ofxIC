#pragma once

#include <functional>

namespace ofxIC {

struct RequestControl {
	int timeoutSeconds = 0;
	std::function<bool()> shouldCancel;
};

enum class RequestFailure {
	None,
	Cancelled,
	Timeout,
	Transport,
	Provider,
	InvalidResponse
};

} // namespace ofxIC
