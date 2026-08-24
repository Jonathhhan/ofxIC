#include "test_harness.h"
#include "../ofxICExample/src/ExampleCredentialStore.h"

#include <utility>

#if defined(_WIN32)
namespace {

class CredentialCleanup {
public:
	explicit CredentialCleanup(std::string name)
		: name_(std::move(name)) {}

	~CredentialCleanup() {
		std::string ignoredError;
		ofxICExample::deleteCredential(name_, ignoredError);
	}

private:
	std::string name_;
};

} // namespace
#endif

OFXIC_TEST(example_credential_store_round_trips_without_plaintext_settings) {
#if defined(_WIN32)
	const std::string name = "OFXIC_CREDENTIAL_TEST_ONLY";
	CredentialCleanup cleanup(name);
	const std::string expected = "test-token-not-a-real-secret";
	std::string value;
	std::string error;
	ofxICExample::deleteCredential(name, error);
	OFXIC_REQUIRE(ofxICExample::saveCredential(name, expected, error));
	OFXIC_REQUIRE(error.empty());
	OFXIC_REQUIRE(ofxICExample::loadCredential(name, value, error));
	OFXIC_REQUIRE(value == expected);
	OFXIC_REQUIRE(ofxICExample::deleteCredential(name, error));
	value.clear();
	OFXIC_REQUIRE(ofxICExample::loadCredential(name, value, error));
	OFXIC_REQUIRE(value.empty());
#else
	OFXIC_REQUIRE(!ofxICExample::credentialStoreAvailable());
#endif
}
