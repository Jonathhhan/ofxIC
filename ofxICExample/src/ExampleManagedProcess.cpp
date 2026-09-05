#include "ExampleManagedProcess.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
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

std::wstring quotePowerShellLiteral(const std::wstring & value) {
	std::wstring result = L"'";
	for (const wchar_t character : value) {
		result.push_back(character);
		if (character == L'\'') result.push_back(L'\'');
	}
	result.push_back(L'\'');
	return result;
}
#endif

} // namespace

struct ManagedProcess::Impl {
	struct FollowedFile {
		std::string path;
		std::uintmax_t offset = 0;
		bool initialized = false;
	};

	ManagedProcessState processState = ManagedProcessState::Stopped;
	std::string processName = "Local process";
	std::string processStatus = "Local process is stopped.";
	std::string processLaunchMethod = "none";
	std::string output;
	std::string newOutput;
	std::string consoleLine;
	bool consoleCarriageReturn = false;
	std::vector<FollowedFile> followedFiles;
	std::vector<std::string> followedPaths;
	unsigned long pid = 0;
	unsigned short port = 0;
	int lastExitCode = 0;
	std::chrono::steady_clock::time_point startedAt{};
	std::function<bool()> readinessProbe;
	std::future<bool> pendingProbe;
	std::chrono::steady_clock::time_point nextProbe{};
	bool protocolReady = false;
	void updateReadiness() {
		if (!readinessProbe) return;
		if (pendingProbe.valid() && pendingProbe.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
			try { protocolReady = pendingProbe.get(); } catch (...) { protocolReady = false; }
			nextProbe = std::chrono::steady_clock::now() + std::chrono::seconds(1);
		}
		if (!pendingProbe.valid() && std::chrono::steady_clock::now() >= nextProbe) {
			const auto probe = readinessProbe;
			pendingProbe = std::async(std::launch::async, [probe]() { return probe(); });
		}
	}
#if defined(_WIN32)
	HANDLE process = nullptr;
	HANDLE outputRead = nullptr;
	HANDLE job = nullptr;
#endif

	void appendOutput(const char * bytes, std::size_t size) {
		if (!bytes || size == 0) return;
		output.append(bytes, size);
		for (std::size_t index = 0; index < size; ++index) {
			const char character = bytes[index];
			if (character == '\r') {
				consoleCarriageReturn = true;
				continue;
			}
			if (character == '\n') {
				newOutput += consoleLine;
				newOutput.push_back('\n');
				consoleLine.clear();
				consoleCarriageReturn = false;
				continue;
			}
			if (consoleCarriageReturn) {
				consoleLine.clear();
				consoleCarriageReturn = false;
			}
			consoleLine.push_back(character);
		}
		if (output.size() > maximumRecentOutput)
			output.erase(0, output.size() - maximumRecentOutput);
		if (newOutput.size() > maximumRecentOutput)
			newOutput.erase(0, newOutput.size() - maximumRecentOutput);
	}

	std::string latestOutputSummary() const {
		std::string line = consoleLine;
		if (line.empty() && !output.empty()) {
			const std::size_t end = output.find_last_not_of("\r\n \t");
			if (end != std::string::npos) {
				const std::size_t separator = output.find_last_of("\r\n", end);
				const std::size_t begin = separator == std::string::npos ? 0 : separator + 1;
				line = output.substr(begin, end - begin + 1);
			}
		}
		for (char & character : line)
			if (character == '\t') character = ' ';
		constexpr std::size_t maximumStatusOutput = 180;
		if (line.size() > maximumStatusOutput)
			line = "..." + line.substr(line.size() - (maximumStatusOutput - 3));
		return line;
	}

	void readFollowedOutput() {
		for (auto & source : followedFiles) {
			std::error_code error;
			const std::filesystem::path path(source.path);
			if (!std::filesystem::is_regular_file(path, error)) continue;
			const std::uintmax_t size = std::filesystem::file_size(path, error);
			if (error) continue;
			if (!source.initialized) {
				source.offset = size > maximumRecentOutput ? size - maximumRecentOutput : 0;
				source.initialized = true;
				const std::string header = "\n--- External log: " + source.path + " ---\n";
				appendOutput(header.data(), header.size());
			} else if (size < source.offset) {
				source.offset = 0;
				const std::string marker = "\n--- Log was replaced or truncated: " +
					source.path + " ---\n";
				appendOutput(marker.data(), marker.size());
			}
			if (size <= source.offset) continue;
			std::uintmax_t begin = source.offset;
			if (size - begin > maximumRecentOutput) begin = size - maximumRecentOutput;
			std::ifstream input(path, std::ios::binary);
			if (!input) continue;
			input.seekg(static_cast<std::streamoff>(begin));
			char buffer[4096];
			while (input) {
				input.read(buffer, sizeof(buffer));
				const std::streamsize count = input.gcount();
				if (count > 0) appendOutput(buffer, static_cast<std::size_t>(count));
			}
			source.offset = size;
		}
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
		if (job) {
			CloseHandle(job);
			job = nullptr;
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

void ManagedProcess::setReadinessProbe(std::function<bool()> probe) {
	impl->readinessProbe = std::move(probe);
	impl->protocolReady = false;
}

bool ManagedProcess::useExisting(const std::string & name, unsigned short readinessPort) {
#if defined(_WIN32)
	if (readinessPort == 0 || !tcpPortIsListening(readinessPort)) return false;
	impl->processName = name.empty() ? "Local process" : name;
	impl->output.clear();
	impl->newOutput.clear();
	impl->followedFiles.clear();
	impl->followedPaths.clear();
	impl->port = readinessPort;
	impl->pid = 0;
	impl->processLaunchMethod = "external";
	impl->startedAt = std::chrono::steady_clock::now();
	impl->protocolReady = false;
	impl->processState = impl->readinessProbe ? ManagedProcessState::Starting : ManagedProcessState::Ready;
	impl->processStatus = impl->processName + " is already reachable at http://127.0.0.1:" +
		std::to_string(readinessPort) + "; using the externally managed process.";
	if (impl->readinessProbe) impl->processStatus = impl->processName + ": listener found; checking protocol readiness.";
	return true;
#else
	(void)name;
	(void)readinessPort;
	return false;
#endif
}

void ManagedProcess::followOutputFiles(const std::vector<std::string> & paths) {
	impl->followedFiles.clear();
	impl->followedPaths.clear();
	for (const std::string & path : paths) {
		if (path.empty()) continue;
		impl->followedFiles.push_back({ path });
		impl->followedPaths.push_back(path);
	}
	impl->readFollowedOutput();
}

void ManagedProcess::clearRecentOutput() {
	impl->output.clear();
	impl->newOutput.clear();
	impl->consoleLine.clear();
	impl->consoleCarriageReturn = false;
}

bool ManagedProcess::start(const std::string & executable,
	const std::vector<std::string> & arguments, const std::string & name,
	unsigned short readinessPort, const std::string & requestedWorkingDirectory) {
	if (running()) return true;
	if (impl->pendingProbe.valid()) {
		try { impl->pendingProbe.get(); } catch (...) {}
	}
	impl->protocolReady = false;
	impl->nextProbe = {};
	impl->processName = name.empty() ? "Local process" : name;
	impl->port = readinessPort;
	impl->lastExitCode = 0;
	impl->processLaunchMethod = "none";
	impl->output.clear();
	impl->newOutput.clear();
	impl->consoleLine.clear();
	impl->consoleCarriageReturn = false;
	impl->followedFiles.clear();
	impl->followedPaths.clear();
	if (useExisting(impl->processName, readinessPort)) return true;
#if !defined(_WIN32)
	std::error_code fileError;
	if (!std::filesystem::is_regular_file(std::filesystem::path(executable), fileError)) {
		impl->processState = ManagedProcessState::Failed;
		impl->processStatus = impl->processName + " executable does not exist: " + executable;
		return false;
	}
#endif
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
	const DWORD directAttributes = GetFileAttributesW(wideExecutable.c_str());
	const bool directExecutableVisible = directAttributes != INVALID_FILE_ATTRIBUTES &&
		(directAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
	std::wstring lowerExecutable = wideExecutable;
	std::transform(lowerExecutable.begin(), lowerExecutable.end(), lowerExecutable.begin(),
		[](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
	const bool installedOfxICRuntime =
		lowerExecutable.find(L"\\ofxic\\servers\\") != std::wstring::npos;
	std::wstring command = quoteWindowsArgument(wideExecutable);
	std::wstring shellParameters;
	for (const auto & argument : arguments) {
		const std::wstring quoted = quoteWindowsArgument(utf8ToWide(argument));
		command += L" " + quoted;
		if (!shellParameters.empty()) shellParameters += L" ";
		shellParameters += quoted;
	}
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	startup.hStdOutput = outputWrite;
	startup.hStdError = outputWrite;
	PROCESS_INFORMATION processInfo{};
	BOOL currentProcessIsInJob = FALSE;
	if (!IsProcessInJob(GetCurrentProcess(), nullptr, &currentProcessIsInJob))
		currentProcessIsInJob = FALSE;
	const bool preferShellBroker = currentProcessIsInJob && installedOfxICRuntime &&
		!directExecutableVisible;
	if (!currentProcessIsInJob) {
		impl->job = CreateJobObjectW(nullptr, nullptr);
		if (!impl->job) {
			const DWORD error = GetLastError();
			CloseHandle(outputWrite);
			impl->closeHandles();
			impl->processState = ManagedProcessState::Failed;
			impl->processStatus = "Could not create a Windows job for " + impl->processName +
				" (Windows error " + std::to_string(error) + ": " + windowsErrorMessage(error) + ").";
			return false;
		}
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimits{};
		jobLimits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (!SetInformationJobObject(impl->job, JobObjectExtendedLimitInformation,
			&jobLimits, sizeof(jobLimits))) {
			const DWORD error = GetLastError();
			CloseHandle(outputWrite);
			impl->closeHandles();
			impl->processState = ManagedProcessState::Failed;
			impl->processStatus = "Could not configure a Windows job for " + impl->processName +
				" (Windows error " + std::to_string(error) + ": " + windowsErrorMessage(error) + ").";
			return false;
		}
	}
	std::filesystem::path workingPath = requestedWorkingDirectory.empty()
		? std::filesystem::path(executable).parent_path()
		: std::filesystem::path(requestedWorkingDirectory);
	const std::wstring workingDirectory = workingPath.wstring();
	BOOL created = FALSE;
	DWORD createError = ERROR_ACCESS_DENIED;
	auto createDirect = [&](const wchar_t * directory) {
		std::vector<wchar_t> mutableCommand(command.begin(), command.end());
		mutableCommand.push_back(L'\0');
		return CreateProcessW(wideExecutable.c_str(), mutableCommand.data(),
			nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, directory,
			&startup, &processInfo);
	};
	if (!preferShellBroker) {
		created = createDirect(workingDirectory.empty() ? nullptr : workingDirectory.c_str());
		createError = created ? ERROR_SUCCESS : GetLastError();
		if (!created && !workingDirectory.empty() &&
			(createError == ERROR_FILE_NOT_FOUND || createError == ERROR_PATH_NOT_FOUND)) {
			created = createDirect(nullptr);
			createError = created ? ERROR_SUCCESS : GetLastError();
			if (created) {
				const std::string marker =
					"Started without the configured working directory.\n";
				impl->appendOutput(marker.data(), marker.size());
			}
		}
	}
	bool shellLaunched = false;
	if (!created && (preferShellBroker || createError == ERROR_FILE_NOT_FOUND ||
		createError == ERROR_PATH_NOT_FOUND || createError == ERROR_ACCESS_DENIED)) {
		// A GUI executable started from a managed development workspace can have a
		// narrower filesystem namespace than Explorer even though both run as the
		// same user. Ask the Windows shell to open the already configured external
		// runtime and retain its process handle for readiness and stop supervision.
		CloseHandle(outputWrite);
		outputWrite = nullptr;
		if (impl->outputRead) {
			CloseHandle(impl->outputRead);
			impl->outputRead = nullptr;
		}
		SHELLEXECUTEINFOW shell{};
		DWORD shellError = createError;
		if (!preferShellBroker) {
			shell.cbSize = sizeof(shell);
			shell.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
			shell.lpVerb = L"open";
			shell.lpFile = wideExecutable.c_str();
			shell.lpParameters = shellParameters.empty() ? nullptr : shellParameters.c_str();
			shell.lpDirectory = workingDirectory.empty() ? nullptr : workingDirectory.c_str();
			shell.nShow = SW_HIDE;
			created = ShellExecuteExW(&shell) && shell.hProcess;
			shellError = created ? ERROR_SUCCESS : GetLastError();
			if (!created && shell.lpDirectory &&
				(shellError == ERROR_FILE_NOT_FOUND || shellError == ERROR_PATH_NOT_FOUND)) {
				shell.lpDirectory = nullptr;
				created = ShellExecuteExW(&shell) && shell.hProcess;
				shellError = created ? ERROR_SUCCESS : GetLastError();
			}
		}
		if (!created && installedOfxICRuntime &&
			(preferShellBroker || shellError == ERROR_FILE_NOT_FOUND || shellError == ERROR_PATH_NOT_FOUND ||
			 shellError == ERROR_ACCESS_DENIED)) {
			// Some managed GUI launches cannot resolve LocalAppData in CreateProcess or
			// ShellExecute, while a system PowerShell process can. Keep PowerShell alive
			// as the supervised parent by invoking the server with the call operator.
			std::wstring script = L"& " + quotePowerShellLiteral(wideExecutable);
			for (const auto & argument : arguments)
				script += L" " + quotePowerShellLiteral(utf8ToWide(argument));
			const std::wstring brokerParameters =
				L"-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command " +
				quoteWindowsArgument(script);
			SHELLEXECUTEINFOW broker{};
			broker.cbSize = sizeof(broker);
			broker.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
			broker.lpVerb = L"open";
			broker.lpFile = L"powershell.exe";
			broker.lpParameters = brokerParameters.c_str();
			broker.nShow = SW_HIDE;
			created = ShellExecuteExW(&broker) && broker.hProcess;
			shellError = created ? ERROR_SUCCESS : GetLastError();
			if (created) {
				shell.hProcess = broker.hProcess;
				const std::string marker = "Started through the PowerShell process broker.\n";
				impl->appendOutput(marker.data(), marker.size());
			}
		}
		if (created) {
			processInfo.hProcess = shell.hProcess;
			processInfo.dwProcessId = GetProcessId(shell.hProcess);
			shellLaunched = true;
			createError = ERROR_SUCCESS;
			if (impl->output.empty()) {
				const std::string marker = "Started through the Windows shell broker.\n";
				impl->appendOutput(marker.data(), marker.size());
			}
		} else {
			createError = shellError;
		}
	}
	if (outputWrite) CloseHandle(outputWrite);
	if (!created) {
		impl->closeHandles();
		impl->processState = ManagedProcessState::Failed;
		if (!directExecutableVisible && !installedOfxICRuntime) {
			impl->processStatus = impl->processName + " executable does not exist: " + executable;
		} else {
			impl->processStatus = "Could not launch " + impl->processName +
				" from the configured executable: " + executable + " (Windows error " +
				std::to_string(createError) + ": " + windowsErrorMessage(createError) +
				"). The file may exist but be unavailable in the GUI process context.";
		}
		return false;
	}
	if (impl->job && !shellLaunched &&
		!AssignProcessToJobObject(impl->job, processInfo.hProcess)) {
		const DWORD error = GetLastError();
		TerminateProcess(processInfo.hProcess, 1);
		WaitForSingleObject(processInfo.hProcess, 3000);
		if (processInfo.hThread) CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
		impl->closeHandles();
		impl->processState = ManagedProcessState::Failed;
		impl->processStatus = "Could not supervise " + impl->processName +
			" as a Windows process tree (Windows error " + std::to_string(error) + ": " +
			windowsErrorMessage(error) + ").";
		return false;
	}
	if (processInfo.hThread) CloseHandle(processInfo.hThread);
	impl->process = processInfo.hProcess;
	impl->pid = processInfo.dwProcessId;
	impl->processLaunchMethod = shellLaunched ? "windows-shell" : "direct";
	impl->startedAt = std::chrono::steady_clock::now();
	impl->processState = ManagedProcessState::Starting;
	impl->processStatus = readinessPort == 0
		? "Running " + impl->processName + " (PID " + std::to_string(impl->pid) +
			", launch=" + impl->processLaunchMethod + ")..."
		: "Starting " + impl->processName + " on port " +
			std::to_string(readinessPort) + " (PID " + std::to_string(impl->pid) +
			", launch=" + impl->processLaunchMethod + ")...";
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
	if (running()) impl->readFollowedOutput();
	if (running()) impl->updateReadiness();
	if (!impl->process) {
		if (running() && impl->readinessProbe) {
			impl->processState = impl->protocolReady ? ManagedProcessState::Ready : ManagedProcessState::Starting;
			impl->processStatus = impl->processName + (impl->protocolReady
				? ": protocol endpoint ready (externally managed)."
				: ": waiting for a successful protocol readiness check; see server log.");
			return;
		}
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
			std::to_string(code) + " (launch=" + impl->processLaunchMethod + ").";
		const std::string lastOutput = impl->latestOutputSummary();
		if (!lastOutput.empty()) impl->processStatus += " Last output: " + lastOutput;
		impl->pid = 0;
		impl->closeHandles();
		return;
	}
	if (impl->port != 0 && (impl->readinessProbe ? impl->protocolReady : tcpPortIsListening(impl->port))) {
		const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::steady_clock::now() - impl->startedAt).count();
		impl->processState = ManagedProcessState::Ready;
		impl->processStatus = impl->processName + " is ready at http://127.0.0.1:" +
			std::to_string(impl->port) + " after " + std::to_string(elapsed) +
			" s (PID " + std::to_string(impl->pid) + ", launch=" +
			impl->processLaunchMethod + ").";
		return;
	}
	impl->processState = ManagedProcessState::Starting;
	const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::steady_clock::now() - impl->startedAt).count();
	impl->processStatus = impl->port == 0
		? impl->processName + " is running for " + std::to_string(elapsed) +
			" s (PID " + std::to_string(impl->pid) + ", launch=" +
			impl->processLaunchMethod + ")."
		: impl->processName + " process is running but its endpoint is not ready after " +
			std::to_string(elapsed) + (impl->readinessProbe ? " s; waiting for protocol readiness on port " : " s; waiting for TCP port ") +
			std::to_string(impl->port) + " (PID " + std::to_string(impl->pid) +
			", launch=" + impl->processLaunchMethod + ").";
	const std::string lastOutput = impl->latestOutputSummary();
	if (!lastOutput.empty()) impl->processStatus += " Last output: " + lastOutput;
#endif
}

void ManagedProcess::stop() {
	// Consume the bounded probe so a previous launch cannot mark a new one ready.
	if (impl->pendingProbe.valid()) {
		try { impl->pendingProbe.get(); } catch (...) {}
	}
	impl->protocolReady = false;
	impl->nextProbe = {};
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
	impl->processLaunchMethod = "none";
	impl->followedFiles.clear();
	impl->followedPaths.clear();
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
const std::string & ManagedProcess::launchMethod() const { return impl->processLaunchMethod; }
const std::string & ManagedProcess::recentOutput() const { return impl->output; }
const std::vector<std::string> & ManagedProcess::followedOutputFiles() const {
	return impl->followedPaths;
}

std::string ManagedProcess::takeNewOutput() {
	std::string result = std::move(impl->newOutput);
	impl->newOutput.clear();
	return result;
}

} // namespace ofxICExample
