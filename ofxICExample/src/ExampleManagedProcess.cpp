#include "ExampleManagedProcess.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#endif

namespace ofxICExample {
namespace {

constexpr std::size_t maximumRecentOutput = 64 * 1024;

#if defined(_WIN32)
std::wstring utf8ToWide(const std::string & value) {
	if (value.empty()) return {};
	const int size = MultiByteToWideChar(
		CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
	if (size <= 0) return std::wstring(value.begin(), value.end());
	std::wstring result(static_cast<std::size_t>(size), L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
		static_cast<int>(value.size()), result.data(), size);
	return result;
}

std::string wideToUtf8(const std::wstring & value) {
	if (value.empty()) return {};
	const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
		static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
	if (size <= 0) return {};
	std::string result(static_cast<std::size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
		result.data(), size, nullptr, nullptr);
	return result;
}

std::wstring quoteWindowsArgument(const std::wstring & value) {
	if (value.find_first_of(L" \t\"") == std::wstring::npos) return value;
	std::wstring result = L"\"";
	std::size_t slashes = 0;
	for (const wchar_t character : value) {
		if (character == L'\\') {
			++slashes;
			continue;
		}
		if (character == L'\"') {
			result.append(slashes * 2 + 1, L'\\');
			result += L'\"';
		} else {
			result.append(slashes, L'\\');
			result += character;
		}
		slashes = 0;
	}
	result.append(slashes * 2, L'\\');
	result += L'\"';
	return result;
}

bool tcpPortIsListening(unsigned short port) {
	static const bool socketsReady = [] {
		WSADATA data{};
		return WSAStartup(MAKEWORD(2, 2), &data) == 0;
	}();
	if (!socketsReady) return false;
	SOCKET connection = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (connection == INVALID_SOCKET) return false;
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	const bool listening = connect(connection,
		reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
	closesocket(connection);
	return listening;
}

std::string windowsErrorMessage(DWORD error) {
	wchar_t * message = nullptr;
	const DWORD size = FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, error, 0, reinterpret_cast<wchar_t *>(&message), 0, nullptr);
	std::wstring text = size && message ? std::wstring(message, size) : std::wstring();
	if (message) LocalFree(message);
	while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
		text.pop_back();
	return wideToUtf8(text);
}
#endif

} // namespace

struct ManagedProcess::Impl {
	ManagedProcessState processState = ManagedProcessState::Stopped;
	std::string processName = "Local process";
	std::string processStatus = "Local process is stopped.";
	std::string output;
	std::string newOutput;
	unsigned long pid = 0;
	unsigned short port = 0;
	int lastExitCode = 0;
	std::chrono::steady_clock::time_point startedAt{};
#if defined(_WIN32)
	HANDLE process = nullptr;
	HANDLE outputRead = nullptr;
#endif

	void appendOutput(const char * bytes, std::size_t size) {
		if (!bytes || size == 0) return;
		output.append(bytes, size);
		newOutput.append(bytes, size);
		if (output.size() > maximumRecentOutput)
			output.erase(0, output.size() - maximumRecentOutput);
		if (newOutput.size() > maximumRecentOutput)
			newOutput.erase(0, newOutput.size() - maximumRecentOutput);
	}

#if defined(_WIN32)
	void readAvailableOutput() {
		if (!outputRead) return;
		for (;;) {
			DWORD available = 0;
			if (!PeekNamedPipe(outputRead, nullptr, 0, nullptr, &available, nullptr) || available == 0)
				break;
			char buffer[4096];
			DWORD read = 0;
			const DWORD requested = std::min<DWORD>(available, sizeof(buffer));
			if (!ReadFile(outputRead, buffer, requested, &read, nullptr) || read == 0) break;
			appendOutput(buffer, read);
		}
	}

	void closeHandles() {
		if (process) {
			CloseHandle(process);
			process = nullptr;
		}
		if (outputRead) {
			CloseHandle(outputRead);
			outputRead = nullptr;
		}
	}
#endif
};

const char * managedProcessStateLabel(ManagedProcessState state) {
	switch (state) {
	case ManagedProcessState::Stopped: return "stopped";
	case ManagedProcessState::Starting: return "starting";
	case ManagedProcessState::Ready: return "ready";
	case ManagedProcessState::Exited: return "exited";
	case ManagedProcessState::Failed: return "failed";
	}
	return "unknown";
}

ManagedProcess::ManagedProcess() : impl(std::make_unique<Impl>()) {}

ManagedProcess::~ManagedProcess() {
	stop();
}

bool ManagedProcess::useExisting(const std::string & name, unsigned short readinessPort) {
#if defined(_WIN32)
	if (readinessPort == 0 || !tcpPortIsListening(readinessPort)) return false;
	impl->processName = name.empty() ? "Local process" : name;
	impl->port = readinessPort;
	impl->pid = 0;
	impl->processState = ManagedProcessState::Ready;
	impl->processStatus = impl->processName + " is already reachable at http://127.0.0.1:" +
		std::to_string(readinessPort) + "; using the externally managed process.";
	return true;
#else
	(void)name;
	(void)readinessPort;
	return false;
#endif
}

bool ManagedProcess::start(const std::string & executable,
	const std::vector<std::string> & arguments, const std::string & name,
	unsigned short readinessPort, const std::string & requestedWorkingDirectory) {
	if (running()) return true;
	impl->processName = name.empty() ? "Local process" : name;
	impl->port = readinessPort;
	impl->lastExitCode = 0;
	impl->output.clear();
	impl->newOutput.clear();
	if (useExisting(impl->processName, readinessPort)) return true;
	std::error_code fileError;
	if (!std::filesystem::is_regular_file(std::filesystem::path(executable), fileError)) {
		impl->processState = ManagedProcessState::Failed;
		impl->processStatus = impl->processName + " executable does not exist: " + executable;
		return false;
	}
#if defined(_WIN32)
	SECURITY_ATTRIBUTES security{};
	security.nLength = sizeof(security);
	security.bInheritHandle = TRUE;
	HANDLE outputWrite = nullptr;
	if (!CreatePipe(&impl->outputRead, &outputWrite, &security, 0) ||
		!SetHandleInformation(impl->outputRead, HANDLE_FLAG_INHERIT, 0)) {
		const DWORD error = GetLastError();
		if (outputWrite) CloseHandle(outputWrite);
		impl->closeHandles();
		impl->processState = ManagedProcessState::Failed;
		impl->processStatus = "Could not create " + impl->processName +
			" output pipe (Windows error " + std::to_string(error) + ": " +
			windowsErrorMessage(error) + ").";
		return false;
	}

	const std::wstring wideExecutable = utf8ToWide(executable);
	std::wstring command = quoteWindowsArgument(wideExecutable);
	for (const auto & argument : arguments)
		command += L" " + quoteWindowsArgument(utf8ToWide(argument));
	std::vector<wchar_t> mutableCommand(command.begin(), command.end());
	mutableCommand.push_back(L'\0');
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	startup.hStdOutput = outputWrite;
	startup.hStdError = outputWrite;
	PROCESS_INFORMATION processInfo{};
	std::filesystem::path workingPath = requestedWorkingDirectory.empty()
		? std::filesystem::path(executable).parent_path()
		: std::filesystem::path(requestedWorkingDirectory);
	std::error_code directoryError;
	if (!workingPath.empty() && !std::filesystem::is_directory(workingPath, directoryError)) {
		CloseHandle(outputWrite);
		impl->closeHandles();
		impl->processState = ManagedProcessState::Failed;
		impl->processStatus = impl->processName + " working directory does not exist: " +
			workingPath.string();
		return false;
	}
	const std::wstring workingDirectory = workingPath.wstring();
	const BOOL created = CreateProcessW(wideExecutable.c_str(), mutableCommand.data(),
		nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
		workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &processInfo);
	const DWORD createError = created ? ERROR_SUCCESS : GetLastError();
	CloseHandle(outputWrite);
	if (!created) {
		impl->closeHandles();
		impl->processState = ManagedProcessState::Failed;
		impl->processStatus = "Could not start " + impl->processName +
			" (Windows error " + std::to_string(createError) + ": " +
			windowsErrorMessage(createError) + ").";
		return false;
	}
	CloseHandle(processInfo.hThread);
	impl->process = processInfo.hProcess;
	impl->pid = processInfo.dwProcessId;
	impl->startedAt = std::chrono::steady_clock::now();
	impl->processState = ManagedProcessState::Starting;
	impl->processStatus = "Starting " + impl->processName + " on port " +
		std::to_string(readinessPort) + " (PID " + std::to_string(impl->pid) + ")...";
	return true;
#else
	(void)arguments;
	impl->processState = ManagedProcessState::Failed;
	impl->processStatus = "GUI process control is currently implemented for Windows.";
	return false;
#endif
}

void ManagedProcess::update() {
#if defined(_WIN32)
	if (!impl->process) {
		if (impl->processState == ManagedProcessState::Ready && impl->pid == 0 &&
			impl->port != 0 && !tcpPortIsListening(impl->port)) {
			impl->processState = ManagedProcessState::Exited;
			impl->processStatus = "Externally managed " + impl->processName +
				" is no longer reachable on port " + std::to_string(impl->port) + ".";
		}
		return;
	}
	impl->readAvailableOutput();
	DWORD code = STILL_ACTIVE;
	if (!GetExitCodeProcess(impl->process, &code) || code != STILL_ACTIVE) {
		impl->readAvailableOutput();
		impl->lastExitCode = static_cast<int>(code);
		impl->processState = code == 0
			? ManagedProcessState::Exited : ManagedProcessState::Failed;
		impl->processStatus = impl->processName + " exited with code " +
			std::to_string(code) + ".";
		impl->pid = 0;
		impl->closeHandles();
		return;
	}
	if (impl->port != 0 && tcpPortIsListening(impl->port)) {
		impl->processState = ManagedProcessState::Ready;
		impl->processStatus = impl->processName + " is ready at http://127.0.0.1:" +
			std::to_string(impl->port) + " (PID " + std::to_string(impl->pid) + ").";
		return;
	}
	impl->processState = ManagedProcessState::Starting;
	const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::steady_clock::now() - impl->startedAt).count();
	impl->processStatus = impl->processName + " is starting; waiting for port " +
		std::to_string(impl->port) + " for " + std::to_string(elapsed) +
		" s (PID " + std::to_string(impl->pid) + ").";
#endif
}

void ManagedProcess::stop() {
	const bool external = impl->processState == ManagedProcessState::Ready && impl->pid == 0;
#if defined(_WIN32)
	if (impl->process) {
		TerminateProcess(impl->process, 0);
		WaitForSingleObject(impl->process, 3000);
		impl->readAvailableOutput();
	}
	impl->closeHandles();
#endif
	impl->pid = 0;
	impl->lastExitCode = 0;
	impl->processState = ManagedProcessState::Stopped;
	impl->processStatus = external
		? "Disconnected from externally managed " + impl->processName +
			"; the external process was left running."
		: impl->processName + " is stopped.";
}

bool ManagedProcess::running() const {
	return impl->processState == ManagedProcessState::Starting ||
		impl->processState == ManagedProcessState::Ready;
}

bool ManagedProcess::ownsProcess() const { return impl->pid != 0; }

ManagedProcessState ManagedProcess::state() const { return impl->processState; }
unsigned long ManagedProcess::processId() const { return impl->pid; }
int ManagedProcess::exitCode() const { return impl->lastExitCode; }
const std::string & ManagedProcess::status() const { return impl->processStatus; }
const std::string & ManagedProcess::recentOutput() const { return impl->output; }

std::string ManagedProcess::takeNewOutput() {
	std::string result = std::move(impl->newOutput);
	impl->newOutput.clear();
	return result;
}

} // namespace ofxICExample
