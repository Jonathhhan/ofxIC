#include "test_harness.h"
#include "../ofxICExample/src/ExampleManagedProcess.h"

#include <chrono>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#if defined(_WIN32)
#include <winsock2.h>
#endif

OFXIC_TEST(example_managed_process_has_explicit_initial_state) {
	ofxICExample::ManagedProcess process;
	OFXIC_REQUIRE(process.state() == ofxICExample::ManagedProcessState::Stopped);
	OFXIC_REQUIRE(!process.running());
	OFXIC_REQUIRE(process.launchMethod() == "none");
	OFXIC_REQUIRE(std::string(ofxICExample::managedProcessStateLabel(process.state())) ==
		"stopped");
}

OFXIC_TEST(example_managed_process_rejects_missing_executable_before_launch) {
	ofxICExample::ManagedProcess process;
	OFXIC_REQUIRE(!process.start("missing-ofxic-runtime.exe", {}, "fixture server", 19099));
	OFXIC_REQUIRE(process.state() == ofxICExample::ManagedProcessState::Failed);
	OFXIC_REQUIRE(process.status().find("does not exist") != std::string::npos);
	OFXIC_REQUIRE(!process.running());
}

OFXIC_TEST(example_managed_process_captures_child_output_and_exit) {
#if defined(_WIN32)
	const char * commandInterpreter = std::getenv("COMSPEC");
	OFXIC_REQUIRE(commandInterpreter && *commandInterpreter);
	ofxICExample::ManagedProcess process;
	OFXIC_REQUIRE(process.start(commandInterpreter,
		{ "/d", "/s", "/c", "echo ofxIC-supervisor-output" },
		"fixture process", 0));
	OFXIC_REQUIRE(process.launchMethod() == "direct");
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	do {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		process.update();
	} while (process.running() && std::chrono::steady_clock::now() < deadline);
	OFXIC_REQUIRE(!process.running());
	OFXIC_REQUIRE(process.state() == ofxICExample::ManagedProcessState::Exited);
	OFXIC_REQUIRE(process.exitCode() == 0);
	OFXIC_REQUIRE(process.recentOutput().find("ofxIC-supervisor-output") != std::string::npos);
	OFXIC_REQUIRE(process.status().find("Last output: ofxIC-supervisor-output") != std::string::npos);
#endif
}

OFXIC_TEST(example_managed_process_uses_reachable_external_server_without_owning_it) {
#if defined(_WIN32)
	WSADATA sockets{};
	OFXIC_REQUIRE(WSAStartup(MAKEWORD(2, 2), &sockets) == 0);
	SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	OFXIC_REQUIRE(listener != INVALID_SOCKET);
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = 0;
	OFXIC_REQUIRE(bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
	OFXIC_REQUIRE(listen(listener, 1) == 0);
	int addressSize = sizeof(address);
	OFXIC_REQUIRE(getsockname(listener, reinterpret_cast<sockaddr *>(&address), &addressSize) == 0);

	ofxICExample::ManagedProcess process;
	OFXIC_REQUIRE(process.useExisting("fixture server", ntohs(address.sin_port)));
	OFXIC_REQUIRE(process.state() == ofxICExample::ManagedProcessState::Ready);
	OFXIC_REQUIRE(process.running());
	OFXIC_REQUIRE(!process.ownsProcess());
	OFXIC_REQUIRE(process.launchMethod() == "external");
	std::atomic<bool> modelReady{false};
	process.setReadinessProbe([&modelReady]() { return modelReady.load(); });
	process.update();
	OFXIC_REQUIRE(process.state() == ofxICExample::ManagedProcessState::Starting);
	modelReady = true;
	const auto healthDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while (process.state() != ofxICExample::ManagedProcessState::Ready &&
		std::chrono::steady_clock::now() < healthDeadline) {
		process.update();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	OFXIC_REQUIRE(process.state() == ofxICExample::ManagedProcessState::Ready);
	process.setReadinessProbe({});
	closesocket(listener);
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	process.update();
	OFXIC_REQUIRE(process.state() == ofxICExample::ManagedProcessState::Exited);
	WSACleanup();
#endif
}

OFXIC_TEST(example_managed_process_tails_external_log_files_incrementally) {
#if defined(_WIN32)
	WSADATA sockets{};
	OFXIC_REQUIRE(WSAStartup(MAKEWORD(2, 2), &sockets) == 0);
	SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	OFXIC_REQUIRE(listener != INVALID_SOCKET);
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = 0;
	OFXIC_REQUIRE(bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
	OFXIC_REQUIRE(listen(listener, 1) == 0);
	int addressSize = sizeof(address);
	OFXIC_REQUIRE(getsockname(listener, reinterpret_cast<sockaddr *>(&address), &addressSize) == 0);

	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const std::filesystem::path log = std::filesystem::temp_directory_path() /
		("ofxIC-managed-process-" + std::to_string(suffix) + ".log");
	{
		std::ofstream output(log, std::ios::binary | std::ios::trunc);
		output << "external-startup-line\n";
	}
	ofxICExample::ManagedProcess process;
	OFXIC_REQUIRE(process.useExisting("fixture server", ntohs(address.sin_port)));
	process.followOutputFiles({ log.string() });
	OFXIC_REQUIRE(process.followedOutputFiles().size() == 1);
	OFXIC_REQUIRE(process.recentOutput().find(log.string()) != std::string::npos);
	OFXIC_REQUIRE(process.recentOutput().find("external-startup-line") != std::string::npos);
	process.takeNewOutput();
	{
		std::ofstream output(log, std::ios::binary | std::ios::app);
		output << "1/3\r2/3\r3/3\nexternal-live-line\n";
	}
	process.update();
	OFXIC_REQUIRE(process.recentOutput().find("external-live-line") != std::string::npos);
	const std::string consoleDelta = process.takeNewOutput();
	OFXIC_REQUIRE(consoleDelta.find("3/3") != std::string::npos);
	OFXIC_REQUIRE(consoleDelta.find("1/3") == std::string::npos);
	OFXIC_REQUIRE(consoleDelta.find("2/3") == std::string::npos);
	process.clearRecentOutput();
	OFXIC_REQUIRE(process.recentOutput().empty());
	process.stop();
	OFXIC_REQUIRE(process.followedOutputFiles().empty());
	closesocket(listener);
	WSACleanup();
	std::filesystem::remove(log);
#endif
}
