#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct ofxICTestCase {
	std::string name;
	std::function<void()> run;
};

inline std::vector<ofxICTestCase> & ofxICTests() {
	static std::vector<ofxICTestCase> tests;
	return tests;
}

struct ofxICRegisterTest {
	ofxICRegisterTest(std::string name, std::function<void()> run) {
		ofxICTests().push_back({ std::move(name), std::move(run) });
	}
};

#define OFXIC_TEST(name) static void name(); static ofxICRegisterTest register_##name(#name, name); static void name()
#define OFXIC_REQUIRE(expr) do { if (!(expr)) throw std::runtime_error("require failed: " #expr); } while(false)
