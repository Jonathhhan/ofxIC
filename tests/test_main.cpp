#include "test_harness.h"

#include <exception>
#include <iostream>

int main() {
	int failed = 0;
	for (const auto & test : ofxICTests()) {
		try {
			test.run();
			std::cout << "[ok] " << test.name << "\n";
		} catch (const std::exception & e) {
			++failed;
			std::cerr << "[fail] " << test.name << ": " << e.what() << "\n";
		}
	}
	return failed == 0 ? 0 : 1;
}
