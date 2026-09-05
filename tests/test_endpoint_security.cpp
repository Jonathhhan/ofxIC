#include "test_harness.h"
#include "../src/ofxIC.h"

#include <atomic>
#include <limits>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <winsock2.h>
#endif

OFXIC_TEST(endpoint_rejects_authenticated_cross_origin_job_urls_before_transport) {
	int calls = 0;
	ofxIC::Endpoint endpoint("https://trusted.example/api", [&](const ofxIC::HttpRequest &) {
		++calls;
		return ofxIC::HttpResponse{};
	});
	endpoint.setBearerToken("private-test-token");
	ofxIC::MediaClient media(endpoint);
	for (const std::string url : {
		"https://foreign.example/job", "https://trusted.example.evil.test/job",
		"https://trusted.example@evil.test/job", "http://trusted.example/job",
		"https://trusted.example:444/job", "https://trusted.example\\@evil.test/job",
		"https://trusted.example\r\nInjected: x/job", "https://trusted.example:/job" }) {
		const auto result = media.poll(url);
		OFXIC_REQUIRE(!result);
		OFXIC_REQUIRE(result.failure == ofxIC::RequestFailure::Validation);
		OFXIC_REQUIRE(result.error.find("private-test-token") == std::string::npos);
	}
	OFXIC_REQUIRE(calls == 0);
}

OFXIC_TEST(endpoint_accepts_same_origin_case_default_ports_and_signed_queries) {
	int calls = 0;
	ofxIC::Endpoint endpoint("https://trusted.example/api", [&](const ofxIC::HttpRequest & request) {
		++calls;
		OFXIC_REQUIRE(request.headers.size() == 1);
		OFXIC_REQUIRE(request.headers[0].second == "Bearer private-test-token");
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"id":"one","status":"queued"})";
		return response;
	});
	endpoint.setBearerToken("private-test-token");
	ofxIC::MediaClient media(endpoint);
	OFXIC_REQUIRE(media.poll("https://TRUSTED.example:443/jobs/one?signature=a%2Fb"));
	OFXIC_REQUIRE(media.poll("https://trusted.example:00443/jobs/one"));
	OFXIC_REQUIRE(media.poll("one"));
	OFXIC_REQUIRE(calls == 3);
}

OFXIC_TEST(endpoint_rejects_header_injection_in_bearer_token) {
	int calls = 0;
	ofxIC::Endpoint endpoint("https://trusted.example", [&](const ofxIC::HttpRequest &) {
		++calls;
		return ofxIC::HttpResponse{};
	});
	for (const std::string token : { std::string("secret\r\nX-Injected: value"),
		std::string("secret\tvalue"), std::string("secret\0value", 12) }) {
		endpoint.setBearerToken(token);
		const auto result = endpoint.inspect();
		OFXIC_REQUIRE(!result);
		OFXIC_REQUIRE(result.failure == ofxIC::RequestFailure::Validation);
		OFXIC_REQUIRE(result.error.find("secret") == std::string::npos);
	}
	OFXIC_REQUIRE(calls == 0);
}

OFXIC_TEST(endpoint_scopes_ipv6_credentials_by_port) {
	int calls = 0;
	ofxIC::Endpoint endpoint("http://[::1]/api", [&](const ofxIC::HttpRequest & request) {
		++calls;
		OFXIC_REQUIRE(request.headers.size() == 1);
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"id":"one","status":"queued"})";
		return response;
	});
	endpoint.setBearerToken("fixture-token");
	ofxIC::MediaClient media(endpoint);
	OFXIC_REQUIRE(media.poll("http://[::1]:80/jobs/one"));
	OFXIC_REQUIRE(media.poll("http://[::1]:81/jobs/one").failure == ofxIC::RequestFailure::Validation);
	OFXIC_REQUIRE(calls == 1);
}

OFXIC_TEST(endpoint_rejects_tampered_fal_poll_and_result_origins) {
	for (bool tamperPoll : { false, true }) {
		int calls = 0;
		ofxIC::Endpoint endpoint("https://router.huggingface.co", [&](const ofxIC::HttpRequest & request) {
			++calls;
			OFXIC_REQUIRE(request.url.find("https://router.huggingface.co/") == 0);
			ofxIC::HttpResponse response;
			response.started = true;
			response.status = 200;
			response.body = R"({"status":"COMPLETED"})";
			return response;
		});
		endpoint.setBearerToken("fixture-token");
		ofxIC::MediaClient media(endpoint);
		ofxIC::MediaJob job;
		job.protocol = ofxIC::MediaProtocol::HuggingFaceFal;
		job.id = "one";
		job.pollUrl = tamperPoll ? "https://foreign.example/status"
			: "https://router.huggingface.co/fal-ai/one/status?_subdomain=queue";
		job.resultUrl = "https://foreign.example/result";
		const auto result = media.poll(job);
		OFXIC_REQUIRE(!result);
		OFXIC_REQUIRE(result.failure == ofxIC::RequestFailure::Validation);
		OFXIC_REQUIRE(calls == (tamperPoll ? 0 : 1));
	}
}

#if defined(_WIN32)
namespace {
class RedirectServer {
public:
	RedirectServer() {
		WSADATA sockets{};
		if (WSAStartup(MAKEWORD(2, 2), &sockets) != 0) return;
		initialized = true;
		listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listener == INVALID_SOCKET) return;
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
			listen(listener, 2) != 0) return;
		int size = sizeof(address);
		if (getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size) != 0) return;
		url = "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port));
		worker = std::thread([this] {
			while (!stop) {
				fd_set readable;
				FD_ZERO(&readable);
				FD_SET(listener, &readable);
				timeval timeout{ 0, 100000 };
				if (select(0, &readable, nullptr, nullptr, &timeout) <= 0) continue;
				SOCKET client = accept(listener, nullptr, nullptr);
				if (client == INVALID_SOCKET) continue;
				DWORD receiveTimeout = 1000;
				setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
					reinterpret_cast<const char *>(&receiveTimeout), sizeof(receiveTimeout));
				std::string request;
				char buffer[2048];
				while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192) {
					int count = recv(client, buffer, sizeof(buffer), 0);
					if (count <= 0) break;
					request.append(buffer, count);
				}
				const int number = ++requests;
				const std::string response = number == 1
					? "HTTP/1.1 302 Found\r\nLocation: " + url + "/redirected\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
					: "HTTP/1.1 200 OK\r\nContent-Length: 11\r\nConnection: close\r\n\r\n{\"data\":[]}";
				std::size_t sent = 0;
				while (sent < response.size()) {
					const int count = send(client, response.data() + sent,
						static_cast<int>(response.size() - sent), 0);
					if (count <= 0) break;
					sent += count;
				}
				closesocket(client);
			}
		});
	}
	~RedirectServer() {
		stop = true;
		if (worker.joinable()) worker.join();
		if (listener != INVALID_SOCKET) closesocket(listener);
		if (initialized) WSACleanup();
	}
	std::string url;
	std::atomic<int> requests{ 0 };
private:
	bool initialized = false;
	SOCKET listener = INVALID_SOCKET;
	std::atomic<bool> stop{ false };
	std::thread worker;
};
} // namespace

OFXIC_TEST(endpoint_windows_transport_does_not_follow_redirects) {
	RedirectServer server;
	OFXIC_REQUIRE(!server.url.empty());
	ofxIC::Endpoint endpoint(server.url);
	endpoint.setBearerToken("fixture-token");
	ofxIC::RequestControl control;
	control.timeoutSeconds = 2;
	const auto result = endpoint.inspect(control);
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.httpStatus == 302);
	OFXIC_REQUIRE(result.failure == ofxIC::RequestFailure::Provider);
	OFXIC_REQUIRE(server.requests == 1);
}

OFXIC_TEST(endpoint_windows_accepts_large_timeout_without_integer_overflow) {
	RedirectServer server;
	OFXIC_REQUIRE(!server.url.empty());
	ofxIC::Endpoint endpoint(server.url);
	ofxIC::RequestControl control;
	control.timeoutSeconds = (std::numeric_limits<int>::max)();
	const auto result = endpoint.inspect(control);
	OFXIC_REQUIRE(result.httpStatus == 302);
	OFXIC_REQUIRE(server.requests == 1);
}
#endif
