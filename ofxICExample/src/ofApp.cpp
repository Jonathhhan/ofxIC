#include "ofApp.h"
#include "ExampleMediaModelPolicy.h"
#include "ExampleMediaContextPolicy.h"
#include "ExampleAtomicFile.h"
#include "ExampleRuntimePaths.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <functional>
#include <map>
#include <sstream>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

namespace {

struct EndpointProfile {
	const char * name;
	const char * url;
	const char * tokenEnvironment;
};

constexpr std::array<EndpointProfile, 5> endpointProfiles{{
	{ "llama-server", "http://127.0.0.1:8080", "OFXIC_API_KEY" },
	{ "LM Studio", "http://127.0.0.1:1234", "OFXIC_API_KEY" },
	{ "Hugging Face", "https://router.huggingface.co/v1", "HF_TOKEN" },
	{ "OpenAI", "https://api.openai.com/v1", "OPENAI_API_KEY" },
	{ "Custom", "", "OFXIC_API_KEY" },
}};

struct MediaBackendProfile {
	const char * name;
	const char * url;
	const char * tokenEnvironment;
	bool supportsImage;
	bool supportsVideo;
	const char * capabilityNote;
};

constexpr std::array<MediaBackendProfile, 3> mediaBackends{{
	{ "OpenAI images", "https://api.openai.com/v1", "OPENAI_API_KEY",
		true, false, "Image generation only; no OpenAI video adapter." },
	{ "Hugging Face / fal-ai", "https://router.huggingface.co", "HF_TOKEN",
		true, true, "Hosted image and queued video; provider credit may be required." },
	{ "stable-diffusion.cpp", "http://127.0.0.1:8081", "OFXIC_API_KEY",
		true, true, "Video support and container format are checked from the loaded model before submission." },
}};

struct MusicBackendProfile {
	const char * name;
	const char * url;
	const char * capabilityNote;
};

constexpr std::array<MusicBackendProfile, 2> musicBackends{{
	{ "ACE-Step local", "http://127.0.0.1:8085",
		"Official ACE-Step 1.5 API: /release_task, /query_result, then /v1/audio." },
	{ "Stability Audio 3", "https://api.stability.ai",
		"Hosted asynchronous Stability AI generation; provider credit is required." },
}};

bool supportsMediaKind(int backend, int kind) {
	if (backend < 0 || backend >= static_cast<int>(mediaBackends.size())) return false;
	return kind == 0
		? mediaBackends[backend].supportsImage
		: kind == 1 && mediaBackends[backend].supportsVideo;
}

std::string unsupportedMediaMessage(int backend, int kind) {
	if (backend < 0 || backend >= static_cast<int>(mediaBackends.size())) {
		return "Unknown media backend";
	}
	return std::string(mediaBackends[backend].name) + " does not support " +
		(kind == 1 ? "video" : "image") + " in ofxIC";
}

std::string environmentValue(const char * name) {
#if defined(_WIN32)
	char * value = nullptr;
	std::size_t length = 0;
	if (_dupenv_s(&value, &length, name) != 0 || !value) return {};
	std::string result(value);
	std::free(value);
	return result;
#else
	const char * value = std::getenv(name);
	return value && *value ? value : "";
#endif
}

std::string redactUserProfile(std::string value) {
	const std::string profile = environmentValue("USERPROFILE");
	if (profile.empty()) return value;
	std::string lowerValue = value;
	std::string lowerProfile = profile;
	std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(),
		[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
	std::transform(lowerProfile.begin(), lowerProfile.end(), lowerProfile.begin(),
		[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
	std::size_t offset = 0;
	while ((offset = lowerValue.find(lowerProfile, offset)) != std::string::npos) {
		value.replace(offset, profile.size(), "%USERPROFILE%");
		lowerValue.replace(offset, lowerProfile.size(), "%userprofile%");
		offset += std::string("%USERPROFILE%").size();
	}
	return value;
}

std::string diagnosticText(std::string value) {
	value = redactUserProfile(std::move(value));
	std::replace(value.begin(), value.end(), '\r', ' ');
	std::replace(value.begin(), value.end(), '\n', ' ');
	if (value.size() > 1024) value.resize(1024);
	return value;
}

std::string diagnosticEndpoint(std::string value) {
	const std::size_t scheme = value.find("://");
	if (scheme != std::string::npos) {
		const std::size_t authority = scheme + 3;
		const std::size_t authorityEnd = value.find_first_of("/?#", authority);
		const std::size_t at = value.find('@', authority);
		if (at != std::string::npos &&
			(authorityEnd == std::string::npos || at < authorityEnd)) {
			value.erase(authority, at - authority + 1);
		}
	}
	const std::size_t privateSuffix = value.find_first_of("?#");
	if (privateSuffix != std::string::npos) value.resize(privateSuffix);
	return diagnosticText(std::move(value));
}

bool usesManagedSamBridge(const std::string & endpoint) {
	return endpoint == "http://127.0.0.1:18085" ||
		endpoint == "http://127.0.0.1:18085/" ||
		endpoint == "http://localhost:18085" ||
		endpoint == "http://localhost:18085/";
}

std::filesystem::path localAppDataDirectory() {
	std::vector<std::filesystem::path> candidates;
	const std::string environmentPath = environmentValue("LOCALAPPDATA");
	if (!environmentPath.empty()) candidates.emplace_back(environmentPath);
#if defined(_WIN32)
	char knownFolder[MAX_PATH]{};
	if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr,
		SHGFP_TYPE_CURRENT, knownFolder)) && knownFolder[0])
		candidates.emplace_back(knownFolder);
#endif
	std::error_code error;
	for (const auto & candidate : candidates)
		if (std::filesystem::is_directory(candidate, error)) return candidate;
	return candidates.empty() ? std::filesystem::path() : candidates.front();
}

std::vector<std::string> localServerLogFiles(const std::string & server, int port) {
	const std::filesystem::path root = localAppDataDirectory() / "ofxIC" / "logs";
	const std::string prefix = server + "-" + std::to_string(port);
	return {
		(root / (prefix + ".stdout.log")).string(),
		(root / (prefix + ".stderr.log")).string(),
	};
}

std::string logFileAge(const std::string & path) {
	std::error_code error;
	const auto modified = std::filesystem::last_write_time(path, error);
	if (error) return "not found yet";
	auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
		std::filesystem::file_time_type::clock::now() - modified).count();
	seconds = std::max<std::int64_t>(0, seconds);
	if (seconds < 60) return "updated " + std::to_string(seconds) + " s ago";
	if (seconds < 3600) return "updated " + std::to_string(seconds / 60) + " min ago";
	return "updated " + std::to_string(seconds / 3600) + " h ago";
}

std::string installedServerRoot();

std::string installedServerExecutable(const std::string & familyPrefix,
	const std::string & executableName) {
	const std::filesystem::path root(installedServerRoot());
	if (root.empty()) return {};
	// These are the stable locations produced by the pinned Windows installers.
	// Probe them first to avoid a recursive traversal for normal installations.
	// Missing preferred files fall back to version-independent discovery.
	std::filesystem::path installed;
	if (familyPrefix == "llama.cpp-" && executableName == "llama-server.exe")
		installed = root / "llama.cpp-b10516-cuda-13.3" / executableName;
	else if (familyPrefix == "stable-diffusion.cpp-" && executableName == "sd-server.exe")
		installed = root / "stable-diffusion.cpp-master-829-0a565f2-cuda12" / executableName;
	else if (familyPrefix == "whisper.cpp-" && executableName == "whisper-server.exe")
		installed = root / "whisper.cpp-b4938-cuda-12.4" / executableName;
	else if (familyPrefix == "acestep.cpp-" && executableName == "ace-server.exe")
		installed = root / "acestep.cpp-9761469-cuda-13" / executableName;
	else if (familyPrefix == "ACE-Step-1.5-" && executableName == "python.exe")
		installed = root / "ACE-Step-1.5-v0.1.8-cuda" / ".venv" / "Scripts" / executableName;
	else if (familyPrefix == "sam-python-" && executableName == "python.exe")
		installed = root / "sam-python-1-cuda-13" / ".venv" / "Scripts" / executableName;
	else if (familyPrefix == "sam-python-" && executableName == "sam-python-runner.py")
		installed = root / "sam-python-1-cuda-13" / executableName;
	return ofxICExample::findInstalledExecutable(
		root.string(), familyPrefix, executableName, installed.string());
}

bool isNativeAceStepServer(const std::string & path) {
	return ofToLower(std::filesystem::path(path).filename().string()) == "ace-server.exe";
}

std::string installedAceStepServer() {
	const std::string native = installedServerExecutable("acestep.cpp-", "ace-server.exe");
	return native.empty()
		? installedServerExecutable("ACE-Step-1.5-", "python.exe")
		: native;
}

bool managedAceStepArguments(const std::string & arguments) {
	return arguments.empty() || arguments.rfind("--models ", 0) == 0 ||
		arguments == "-m acestep.api_server --host 127.0.0.1 --port 8085";
}

std::string defaultAceStepArguments(const std::string & executable,
	const std::string & modelDirectory) {
	if (!isNativeAceStepServer(executable))
		return "-m acestep.api_server --host 127.0.0.1 --port 8085";
	return "--models \"" + modelDirectory + "\" --host 127.0.0.1 --port 8085";
}

bool hasNativeAceStepModels(const std::string & directory) {
	std::error_code error;
	if (!std::filesystem::is_directory(directory, error)) return false;
	bool lm = false;
	bool embedding = false;
	bool dit = false;
	bool vae = false;
	const auto gguf = [](const std::string & name) {
		return name.size() >= 5U && name.compare(name.size() - 5U, 5U, ".gguf") == 0;
	};
	for (const auto & entry : std::filesystem::directory_iterator(directory, error)) {
		if (error) return false;
		if (!entry.is_regular_file(error)) continue;
		const std::string name = ofToLower(entry.path().filename().string());
		if (name.rfind("acestep-5hz-lm-", 0) == 0 && gguf(name)) lm = true;
		else if (name.rfind("qwen3-embedding-", 0) == 0 && gguf(name)) embedding = true;
		else if (name.rfind("acestep-v15-", 0) == 0 && gguf(name)) dit = true;
		else if (name.rfind("vae-", 0) == 0 && gguf(name)) vae = true;
	}
	return lm && embedding && dit && vae;
}

bool configuredPath(const char * path) {
	return path && path[0] != '\0';
}

bool configuredRuntime(const char * configured,
	const std::string & startupDetected) {
	// Runtime paths under LocalAppData can be launchable through the Windows
	// shell even when a managed GUI process cannot stat them. Configuration is
	// therefore based on the selected/detected absolute path; ManagedProcess is
	// responsible for reporting the real Windows launch error.
	return configuredPath(configured) || !startupDetected.empty();
}

std::string preferredNativeAceStepSynthModel(const std::string & directory) {
	std::vector<std::string> turbo;
	std::vector<std::string> fallback;
	std::error_code error;
	if (!std::filesystem::is_directory(directory, error)) return {};
	for (const auto & entry : std::filesystem::directory_iterator(directory, error)) {
		if (error) return {};
		std::error_code entryError;
		if (!entry.is_regular_file(entryError) || entryError) continue;
		const std::string filename = entry.path().filename().string();
		const std::string lower = ofToLower(filename);
		if (lower.rfind("acestep-v15-", 0) != 0 ||
			lower.size() < 5U || lower.compare(lower.size() - 5U, 5U, ".gguf") != 0)
			continue;
		(lower.find("turbo") != std::string::npos ? turbo : fallback).push_back(filename);
	}
	auto choose = [](std::vector<std::string> & models) {
		std::sort(models.begin(), models.end());
		return models.empty() ? std::string{} : models.front();
	};
	std::string selected = choose(turbo);
	return selected.empty() ? choose(fallback) : selected;
}

std::string installedServerRoot() {
	const std::filesystem::path localRuntime =
		std::filesystem::path(ofFilePath::getCurrentExeDir()) / "runtime" / "servers";
	std::error_code localError;
	if (std::filesystem::is_directory(localRuntime, localError))
		return localRuntime.lexically_normal().string();
	const std::filesystem::path localAppData = localAppDataDirectory();
	return localAppData.empty()
		? std::string()
		: (localAppData / "ofxIC" / "servers").string();
}

std::string runtimeExecutableFailureDiagnostic(const std::string & configured,
	const std::string & startupDetected, const std::string & familyPrefix,
	const std::string & executableName) {
	const auto state = [](const std::string & path) {
		return path.empty() ? std::string("empty") :
			(ofxICExample::executableFileExists(path) ? "file:" + path : "missing:" + path);
	};
	return "configured=" + state(configured) + "; startup=" + state(startupDetected) +
		"; " + ofxICExample::installedExecutableSearchDiagnostic(
			installedServerRoot(), familyPrefix, executableName);
}

std::vector<std::string> nonEmptyLines(const std::string & value) {
	std::vector<std::string> result;
	std::istringstream input(value);
	std::string line;
	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (!line.empty()) result.push_back(std::move(line));
	}
	return result;
}

std::string installedLlamaModel() {
	const std::filesystem::path localModel =
		std::filesystem::path(ofFilePath::getCurrentExeDir()) / "runtime" / "models" /
		"llama.cpp" / "qwen2.5-1.5b-instruct-q4_k_m.gguf";
	std::error_code localError;
	if (std::filesystem::is_regular_file(localModel, localError))
		return localModel.lexically_normal().string();
	const std::filesystem::path localAppData = localAppDataDirectory();
	if (localAppData.empty()) return {};
	const std::string root = (localAppData / "ofxIC" / "models" / "llama.cpp").string();
	// This is the stable destination produced by the pinned model installer.
	// Return it directly: a managed GUI process can be unable to stat LocalAppData
	// even though the external runtime launched by the shell can read the file.
	return ofFilePath::join(root, "qwen2.5-1.5b-instruct-q4_k_m.gguf");
}

std::vector<std::string> detectedLlamaModelPaths(const std::string & additionalRoot) {
	std::vector<std::string> result;
	std::vector<std::string> roots;
	const std::filesystem::path localAppData = localAppDataDirectory();
	if (!localAppData.empty())
		roots.push_back((localAppData / "ofxIC" / "models" / "llama.cpp").string());
	if (!additionalRoot.empty() &&
		std::find(roots.begin(), roots.end(), additionalRoot) == roots.end())
		roots.push_back(additionalRoot);
	for (const std::string & root : roots) {
		ofDirectory models;
		models.openFromCWD(root);
		if (!models.exists()) continue;
		models.allowExt("gguf");
		models.listDir();
		models.sort();
		for (const ofFile & model : models.getFiles()) result.push_back(model.getAbsolutePath());
	}
	return result;
}

void scanStableDiffusionModels(const std::string & root,
	std::vector<std::string> & diffusion, std::vector<std::string> & vae,
	std::vector<std::string> & encoders) {
	diffusion.clear(); vae.clear(); encoders.clear();
	if (root.empty()) return;
	ofDirectory models; models.openFromCWD(root);
	if (!models.exists()) return;
	models.allowExt("gguf"); models.listDir(); models.sort();
	models.allowExt("safetensors");
	models.allowExt("sft");
	models.allowExt("ckpt");
	models.listDir(); models.sort();
	for (const ofFile & model : models.getFiles()) {
		const std::string path = model.getAbsolutePath();
		const std::string name = ofToLower(model.getFileName());
		if (name.find("vae") != std::string::npos) vae.push_back(path);
		else if (name.find("umt5") != std::string::npos || name.find("text") != std::string::npos ||
			name.find("clip") != std::string::npos) encoders.push_back(path);
		else if (name.find("sd") != std::string::npos || name.find("wan") != std::string::npos ||
			name.find("flux") != std::string::npos) diffusion.push_back(path);
	}
}

std::string preferredModelComponent(const std::vector<std::string> & candidates,
	const std::vector<std::string> & preferredNames) {
	for (const std::string & preferred : preferredNames) {
		for (const std::string & candidate : candidates) {
			if (ofToLower(ofFilePath::getFileName(candidate)).find(preferred) != std::string::npos)
				return candidate;
		}
	}
	return candidates.empty() ? std::string{} : candidates.front();
}

std::string bundledAddonSdTurboCheckpoint() {
	std::filesystem::path directory(ofFilePath::getCurrentExeDir());
	for (int level = 0; level < 6 && !directory.empty(); ++level) {
		const auto candidate = directory / "ofxGgmlDiffusion" /
			"ofxGgmlDiffusionPromptExample" / "bin" / "data" / "models" /
			"sd_turbo.safetensors";
		std::error_code error;
		if (std::filesystem::is_regular_file(candidate, error))
			return candidate.lexically_normal().string();
		directory = directory.parent_path();
	}
	return {};
}

std::string bundledScript(const std::string & filename) {
	std::filesystem::path directory(ofFilePath::getCurrentExeDir());
	for (int level = 0; level < 8 && !directory.empty(); ++level) {
		for (const auto & relative : {
			std::filesystem::path("scripts") / filename,
			std::filesystem::path("addons") / "ofxIC" / "scripts" / filename }) {
			const auto candidate = directory / relative;
			std::error_code error;
			if (std::filesystem::is_regular_file(candidate, error))
				return candidate.lexically_normal().string();
		}
		directory = directory.parent_path();
	}
	return {};
}

std::string powerShellLiteral(const std::string & value) {
	std::string quoted = "'";
	for (char character : value) {
		quoted += character;
		if (character == '\'') quoted += '\'';
	}
	return quoted + "'";
}

std::string runtimeInstallerCommand(const std::string & scriptName,
	const std::string & extraArguments = {}, bool plan = false) {
	const std::string resolved = bundledScript(scriptName);
	if (resolved.empty()) return {};
	std::string command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File " +
		powerShellLiteral(resolved);
	if (!extraArguments.empty()) command += " " + extraArguments;
	if (plan) command += " -Plan";
	return command;
}

std::string runtimeInstallIdentity(const std::string & executable,
	int parentLevels = 1) {
	if (executable.empty()) return "not detected";
	std::filesystem::path directory = std::filesystem::path(executable).parent_path();
	for (int level = 1; level < parentLevels && directory.has_parent_path(); ++level)
		directory = directory.parent_path();
	const std::string identity = directory.filename().string();
	return identity.empty() ? "installed" : identity;
}

std::string bundledSamBridgeScript() {
	return bundledScript("sam-bridge-server.py");
}

std::string firstModelFile(const std::vector<std::filesystem::path> & roots,
	const std::function<bool(const std::filesystem::path &)> & accepts) {
	std::vector<std::filesystem::path> candidates;
	for (const auto & root : roots) {
		std::error_code error;
		if (!std::filesystem::is_directory(root, error)) continue;
		for (std::filesystem::recursive_directory_iterator iterator(root,
			std::filesystem::directory_options::skip_permission_denied, error), end;
			iterator != end; iterator.increment(error)) {
			if (error) { error.clear(); continue; }
			std::error_code entryError;
			if (iterator->is_regular_file(entryError) && accepts(iterator->path()))
				candidates.push_back(iterator->path());
		}
	}
	if (candidates.empty()) return {};
	std::sort(candidates.begin(), candidates.end());
	return candidates.front().lexically_normal().string();
}

std::string detectedWhisperModel() {
	const auto localRuntime = std::filesystem::path(ofFilePath::getCurrentExeDir()) /
		"runtime" / "models" / "whisper.cpp" / "ggml-base-q5_1.bin";
	std::error_code runtimeError;
	if (std::filesystem::is_regular_file(localRuntime, runtimeError))
		return localRuntime.lexically_normal().string();
	const auto local = localAppDataDirectory();
	if (!local.empty()) {
		return (local / "ofxIC" / "models" / "whisper.cpp" /
			"ggml-base-q5_1.bin").lexically_normal().string();
	}
	std::vector<std::filesystem::path> roots;
	roots.emplace_back("G:/Models");
	return firstModelFile(roots, [](const std::filesystem::path & path) {
		const std::string name = ofToLower(path.filename().string());
		return path.extension() == ".bin" &&
			(name.rfind("ggml-", 0) == 0 || name.find("whisper") != std::string::npos);
	});
}

std::string detectedSamModel() {
	std::vector<std::filesystem::path> roots;
	const auto local = localAppDataDirectory();
	if (!local.empty()) roots.push_back(local / "ofxIC" / "models" / "sam");
	roots.emplace_back("G:/Models");
	return firstModelFile(roots, [](const std::filesystem::path & path) {
		const std::string name = ofToLower(path.filename().string());
		return path.extension() == ".pth" && name.rfind("sam_vit_", 0) == 0;
	});
}

#if defined(_WIN32)
std::string executableOnPath(const char * name) {
	char resolved[MAX_PATH]{};
	const DWORD length = SearchPathA(nullptr, name, nullptr, MAX_PATH, resolved, nullptr);
	return length > 0 && length < MAX_PATH ? std::string(resolved, length) : std::string{};
}

std::wstring utf8ToWide(const std::string & value) {
	if (value.empty()) return {};
	const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
	if (size <= 0) return {};
	std::wstring result(static_cast<std::size_t>(size), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
	result.pop_back();
	return result;
}

std::vector<std::string> splitWindowsArguments(const std::string & value) {
	if (value.empty()) return {};
	int count = 0;
	LPWSTR * parsed = CommandLineToArgvW(utf8ToWide("ofxIC " + value).c_str(), &count);
	std::vector<std::string> result;
	if (!parsed) return result;
	for (int index = 1; index < count; ++index) {
		const int size = WideCharToMultiByte(CP_UTF8, 0, parsed[index], -1, nullptr, 0, nullptr, nullptr);
		std::string argument(static_cast<std::size_t>(std::max(0, size)), '\0');
		if (size > 0) {
			WideCharToMultiByte(CP_UTF8, 0, parsed[index], -1, argument.data(), size, nullptr, nullptr);
			argument.pop_back();
		}
		result.push_back(std::move(argument));
	}
	LocalFree(parsed);
	return result;
}
#endif

std::string tokenSetupHint(const std::string & variable) {
#if defined(_WIN32)
	return "PowerShell before launch: $env:" + variable + " = \"your_token\"";
#else
	return "Shell before launch: export " + variable + "=your_token";
#endif
}

std::map<std::string, std::string> settingsEnvironment() {
	std::map<std::string, std::string> values;
	constexpr std::array<const char *, 19> names{{
		"OFXIC_ENDPOINT_URL",
		"OFXIC_MODEL",
		"OFXIC_TRANSCRIPTION_AUTORUN",
		"OFXIC_TRANSCRIPTION_ENDPOINT_URL",
		"OFXIC_TRANSCRIPTION_MODEL",
		"OFXIC_SEGMENTATION_ENDPOINT_URL",
		"OFXIC_MEDIA_BACKEND",
		"OFXIC_MEDIA_ENDPOINT_URL",
		"OFXIC_MEDIA_IMAGE_MODEL",
		"OFXIC_MEDIA_VIDEO_MODEL",
		"OFXIC_MEDIA_KIND",
		"OFXIC_MEDIA_WIDTH",
		"OFXIC_MEDIA_HEIGHT",
		"OFXIC_MEDIA_FRAMES",
		"OFXIC_MEDIA_FPS",
		"OFXIC_MUSIC_BACKEND",
		"OFXIC_MUSIC_ENDPOINT_URL",
		"OFXIC_MUSIC_DURATION",
		"OFXIC_MUSIC_OUTPUT_FORMAT",
	}};
	for (const char * name : names) {
		const std::string value = environmentValue(name);
		if (!value.empty()) values.emplace(name, value);
	}
	return values;
}

std::string configuredSettingsPath() {
	const std::string configured = environmentValue("OFXIC_SETTINGS_PATH");
	if (!configured.empty()) return configured;
	return ofFilePath::join(ofFilePath::getUserHomeDir(), ".ofxICExample.settings");
}

std::string documentSourceName(const std::string & path) {
	const std::size_t separator = path.find_last_of("/\\");
	return separator == std::string::npos ? path : path.substr(separator + 1);
}

bool supportedDocumentPath(const std::string & path) {
	const std::string source = documentSourceName(path);
	const std::size_t dot = source.find_last_of('.');
	if (dot == std::string::npos) return false;
	std::string extension = source.substr(dot);
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return extension == ".md" || extension == ".txt";
}

std::string decodeBase64(const std::string & encoded) {
	static const std::string alphabet =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string decoded;
	unsigned int value = 0;
	int bits = -8;
	for (const unsigned char character : encoded) {
		if (character == '=') break;
		const std::size_t position = alphabet.find(static_cast<char>(character));
		if (position == std::string::npos) {
			if (std::isspace(character)) continue;
			return {};
		}
		value = (value << 6U) + static_cast<unsigned int>(position);
		bits += 6;
		if (bits >= 0) {
			decoded.push_back(static_cast<char>((value >> bits) & 0xff));
			bits -= 8;
		}
	}
	return decoded;
}

const char * mediaJobStateLabel(ofxIC::MediaJobState state) {
	switch (state) {
	case ofxIC::MediaJobState::Queued: return "queued";
	case ofxIC::MediaJobState::Generating: return "generating";
	case ofxIC::MediaJobState::Completed: return "completed";
	case ofxIC::MediaJobState::Failed: return "failed";
	case ofxIC::MediaJobState::Cancelled: return "cancelled";
	default: return "unknown";
	}
}

template <std::size_t Size>
void setTextBuffer(std::array<char, Size> & destination, const std::string & value) {
	static_assert(Size > 0, "text buffers need room for a terminator");
	const std::size_t length = std::min(value.size(), Size - 1);
	std::copy_n(value.data(), length, destination.data());
	destination[length] = '\0';
}

std::string timestampedOutputFilename(const char * kind, const std::string & extension) {
	const std::string stem = "ofxIC-" + std::string(kind) + "-" +
		ofGetTimestampString("%Y%m%d-%H%M%S-%i");
	return extension.empty() ? stem : stem + "." + extension;
}

void writeAutomationResult(const std::string & status, const std::string & output) {
	const std::string path = environmentValue("OFXIC_GUI_RESULT_PATH");
	if (path.empty()) return;
	std::ofstream result(path, std::ios::binary | std::ios::trunc);
	if (!result) {
		ofLogError("ofxIC") << "Could not write GUI result to " << path;
		return;
	}
	result << status << "\n" << output << "\n";
}

void writeMediaAutomationResult(const std::string & status, const std::string & output) {
	const std::string path = environmentValue("OFXIC_MEDIA_RESULT_PATH");
	if (path.empty()) return;
	std::ofstream result(path, std::ios::binary | std::ios::trunc);
	if (!result) {
		ofLogError("ofxIC") << "Could not write media GUI result to " << path;
		return;
	}
	result << status << "\n" << output << "\n";
}

void writeStreamingAutomationResult(const std::string & output) {
	const std::string path = environmentValue("OFXIC_STREAM_RESULT_PATH");
	if (path.empty()) return;
	std::ofstream result(path, std::ios::binary | std::ios::trunc);
	if (result) result << output;
}

void writeMusicAutomationResult(const std::string & status, const std::string & output) {
	const std::string path = environmentValue("OFXIC_MUSIC_RESULT_PATH");
	if (path.empty()) return;
	std::ofstream result(path, std::ios::binary | std::ios::trunc);
	if (!result) {
		ofLogError("ofxIC") << "Could not write music GUI result to " << path;
		return;
	}
	result << status << "\n" << output << "\n";
}

void writeDocumentAutomationResult(
	const std::string & status,
	const std::vector<std::string> & sources) {
	const std::string path = environmentValue("OFXIC_DOCUMENT_RESULT_PATH");
	if (path.empty()) return;
	std::ofstream result(path, std::ios::binary | std::ios::trunc);
	if (!result) {
		ofLogError("ofxIC") << "Could not write document GUI result to " << path;
		return;
	}
	result << status << "\n";
	for (const std::string & source : sources) result << source << "\n";
}

} // namespace

ofApp::ofApp()
	: endpoint("http://127.0.0.1:8080")
	, transcriptionEndpoint("http://127.0.0.1:8080")
	, segmentationEndpoint("http://127.0.0.1:18085")
	, mediaEndpoint("http://127.0.0.1:8080")
	, musicEndpoint("http://127.0.0.1:8085")
	, chat(endpoint)
	, media(mediaEndpoint)
	, stabilityMusic(musicEndpoint)
	, aceStepMusic(musicEndpoint)
	, transcription(transcriptionEndpoint)
	, segmentation(segmentationEndpoint)
	, toolLoop(chat, tools) {
	settingsPath = configuredSettingsPath();
	historyPath = environmentValue("OFXIC_HISTORY_PATH");
	if (historyPath.empty()) historyPath = settingsPath + ".history";
	jobHistory.load(historyPath);
	ofxICExample::ExampleSettings settings;
	const auto loadStatus = ofxICExample::loadSettings(settingsPath, settings);
	if (loadStatus == ofxICExample::SettingsLoadStatus::Loaded) {
		settingsStatus = "Loaded saved non-secret settings.";
	} else if (loadStatus == ofxICExample::SettingsLoadStatus::Invalid) {
		settingsStatus = "Ignored corrupt settings; using safe defaults.";
	} else {
		settingsStatus = "Using built-in defaults; no saved settings yet.";
	}
	ofxICExample::applyEnvironmentOverrides(settings, settingsEnvironment());
	applySettingsToUi(settings);
	if (ofxICExample::credentialStoreAvailable()) {
		for (const char * variable : {
			"HF_TOKEN", "OPENAI_API_KEY", "STABILITY_API_KEY", "OFXIC_API_KEY" }) {
			std::string token;
			std::string error;
			if (ofxICExample::loadCredential(variable, token, error) && !token.empty()) {
				storedTokens[variable] = std::move(token);
			} else if (!error.empty()) {
				credentialStatus = error;
			}
		}
	}
	endpoint.setBaseUrl(endpointUrl.data());
	transcriptionEndpoint.setBaseUrl(transcriptionEndpointUrl.data());
	segmentationEndpoint.setBaseUrl(segmentationEndpointUrl.data());
	mediaEndpoint.setBaseUrl(mediaEndpointUrl.data());
	musicEndpoint.setBaseUrl(musicEndpointUrl.data());
	endpoint.setBearerToken(configuredToken());
	transcriptionEndpoint.setBearerToken(configuredTranscriptionToken());
	segmentationEndpoint.setBearerToken(configuredSegmentationToken());
	mediaEndpoint.setBearerToken(configuredMediaToken());
	musicEndpoint.setBearerToken(configuredMusicToken());
	applyLocalRuntimeDefaults();
}

void ofApp::rescanInstalledRuntimes() {
	// Refresh only discovery evidence. Never reapply settings, choose models,
	// overwrite selected executables, or restart a process during a rescan.
	detectedLlamaServerPath = installedServerExecutable("llama.cpp-", "llama-server.exe");
	detectedStableDiffusionServerPath = installedServerExecutable("stable-diffusion.cpp-", "sd-server.exe");
	detectedAceStepServerPath = installedAceStepServer();
	detectedWhisperServerPath = installedServerExecutable("whisper.cpp-", "whisper-server.exe");
	detectedSamPythonPath = installedServerExecutable("sam-python-", "python.exe");
	detectedSamRunnerPath = installedServerExecutable("sam-python-", "sam-python-runner.py");
	ofLogNotice("ofxIC servers") << "search root: " << installedServerRoot();
}

void ofApp::applyLocalRuntimeDefaults() {
	rescanInstalledRuntimes();
	const std::string & installedLlamaServer = detectedLlamaServerPath;
	if (!installedLlamaServer.empty()) {
		if (!ofxICExample::executableFileExists(llamaServerPath.data()))
			setTextBuffer(llamaServerPath, installedLlamaServer);
		llamaServerStatus = "Script-installed llama-server detected.";
		ofLogNotice("ofxIC servers") << "llama-server: " << installedLlamaServer;
	} else {
		ofLogWarning("ofxIC servers") << "llama-server not found\n" <<
			ofxICExample::installedExecutableSearchDiagnostic(installedServerRoot(),
				"llama.cpp-", "llama-server.exe");
	}
	const std::string defaultLlamaModel = installedLlamaModel();
	if (!configuredPath(llamaModelPath.data()) && !defaultLlamaModel.empty()) {
		setTextBuffer(llamaModelPath, defaultLlamaModel);
		llamaServerStatus = "Script-installed llama-server and local GGUF model detected.";
	}
	ofDirectory externalModels;
	externalModels.openFromCWD("G:/Models");
	if (!llamaModelDirectory[0] && externalModels.exists())
		setTextBuffer(llamaModelDirectory, "G:/Models");
	detectedLlamaModels = detectedLlamaModelPaths(llamaModelDirectory.data());
	const std::string & installedSdServer = detectedStableDiffusionServerPath;
	if (!installedSdServer.empty()) {
		if (!ofxICExample::executableFileExists(stableDiffusionServerPath.data()))
			setTextBuffer(stableDiffusionServerPath, installedSdServer);
		stableDiffusionServerStatus = "Script-installed sd-server detected.";
		ofLogNotice("ofxIC servers") << "sd-server: " << installedSdServer;
	} else {
		ofLogWarning("ofxIC servers") << "sd-server not found\n" <<
			ofxICExample::installedExecutableSearchDiagnostic(installedServerRoot(),
				"stable-diffusion.cpp-", "sd-server.exe");
	}
	if (!aceStepModelDirectory[0] && externalModels.exists())
		setTextBuffer(aceStepModelDirectory, "G:/Models");
	const std::string & installedAceStep = detectedAceStepServerPath;
	if (!installedAceStep.empty()) {
		const bool oldManagedPython = !isNativeAceStepServer(aceStepServerPath.data()) &&
			managedAceStepArguments(aceStepServerArguments.data());
		if (!ofxICExample::executableFileExists(aceStepServerPath.data()) ||
			(isNativeAceStepServer(installedAceStep) && oldManagedPython))
			setTextBuffer(aceStepServerPath, installedAceStep);
		if (managedAceStepArguments(aceStepServerArguments.data()))
			setTextBuffer(aceStepServerArguments, defaultAceStepArguments(
				aceStepServerPath.data(), aceStepModelDirectory.data()));
		aceStepServerStatus = isNativeAceStepServer(aceStepServerPath.data())
			? (hasNativeAceStepModels(aceStepModelDirectory.data())
				? "Native acestep.cpp server and complete GGUF model set detected."
				: "Native acestep.cpp server detected; choose a folder with LM, embedding, DiT and VAE GGUFs.")
			: "Script-installed ACE-Step 1.5 Python environment detected.";
		ofLogNotice("ofxIC servers") << "ACE-Step: " << installedAceStep;
	} else {
		ofLogWarning("ofxIC servers") <<
			"ACE-Step server not found; run scripts\\install-acestep-server.ps1\nNative: " <<
			ofxICExample::installedExecutableSearchDiagnostic(installedServerRoot(),
				"acestep.cpp-", "ace-server.exe") << "\nPython fallback: " <<
			ofxICExample::installedExecutableSearchDiagnostic(installedServerRoot(),
				"ACE-Step-1.5-", "python.exe");
	}
	const std::string & installedWhisper = detectedWhisperServerPath;
	if (!installedWhisper.empty()) {
		if (!ofxICExample::executableFileExists(whisperServerPath.data()))
			setTextBuffer(whisperServerPath, installedWhisper);
		whisperServerStatus = "Script-installed whisper-server detected; select a model.";
		ofLogNotice("ofxIC servers") << "whisper-server: " << installedWhisper;
	} else {
		ofLogWarning("ofxIC servers") <<
			"whisper-server not found; run scripts\\install-whisper-server.ps1\n" <<
			ofxICExample::installedExecutableSearchDiagnostic(installedServerRoot(),
				"whisper.cpp-", "whisper-server.exe");
	}
	const std::string whisperModel = detectedWhisperModel();
	if (!configuredPath(whisperModelPath.data()) && !whisperModel.empty()) {
		setTextBuffer(whisperModelPath, whisperModel);
		whisperServerStatus = installedWhisper.empty()
			? "Whisper model detected; install whisper-server to start."
			: "Script-installed whisper-server and model detected.";
	}
	const std::string samBridge = bundledSamBridgeScript();
#if defined(_WIN32)
	const std::string & installedSamPython = detectedSamPythonPath;
	const std::string & installedSamRunner = detectedSamRunnerPath;
	const std::string python = installedSamPython.empty()
		? executableOnPath("python.exe") : installedSamPython;
	if (!python.empty() && !samBridge.empty() &&
		!ofxICExample::executableFileExists(samBridgeExecutablePath.data()))
		setTextBuffer(samBridgeExecutablePath, python);
	if (!installedSamRunner.empty() &&
		!ofFile::doesFileExist(samRunnerPath.data()))
		setTextBuffer(samRunnerPath, installedSamRunner);
	const std::string samModel = detectedSamModel();
	if (!samModel.empty() && !ofFile::doesFileExist(samModelPath.data()))
		setTextBuffer(samModelPath, samModel);
	if (!installedSamPython.empty() && !installedSamRunner.empty()) {
		samBridgeProcessStatus = samModelPath[0]
			? "Script-installed CUDA SAM runtime and checkpoint detected."
			: "Script-installed CUDA SAM runtime detected; select a checkpoint.";
		ofLogNotice("ofxIC servers") << "SAM Python: " << installedSamPython;
		ofLogNotice("ofxIC servers") << "SAM runner: " << installedSamRunner;
	} else if (!python.empty() && !samBridge.empty()) {
		samBridgeProcessStatus = "SAM bridge found, but its inference runtime is missing. Run scripts\\install-sam-server.ps1.";
	}
#endif
	const std::string addonSdTurbo = bundledAddonSdTurboCheckpoint();
	if (!ofFile::doesFileExist(stableDiffusionModelPath.data()) && !addonSdTurbo.empty()) {
		setTextBuffer(stableDiffusionModelDirectory, ofFilePath::getEnclosingDirectory(addonSdTurbo, false));
		setTextBuffer(stableDiffusionModelPath, addonSdTurbo);
		stableDiffusionCompleteCheckpoint = true;
		scanStableDiffusionModels(stableDiffusionModelDirectory.data(), detectedDiffusionModels,
			detectedVaeModels, detectedTextEncoders);
		stableDiffusionServerStatus = "Addons SD-Turbo complete checkpoint detected.";
	} else {
		if (!stableDiffusionModelDirectory[0] && externalModels.exists())
			setTextBuffer(stableDiffusionModelDirectory, "G:/Models");
		scanStableDiffusionModels(stableDiffusionModelDirectory.data(), detectedDiffusionModels,
			detectedVaeModels, detectedTextEncoders);
		if (!ofFile::doesFileExist(stableDiffusionModelPath.data()) &&
			!detectedDiffusionModels.empty())
			setTextBuffer(stableDiffusionModelPath, detectedDiffusionModels.front());
	}
	const auto applyNonEmptyEnvironment = [](const char * name, auto & destination) {
		const std::string value = environmentValue(name);
		if (!value.empty()) setTextBuffer(destination, value);
	};
	applyNonEmptyEnvironment("OFXIC_LLAMA_SERVER", llamaServerPath);
	applyNonEmptyEnvironment("OFXIC_LLAMA_MODEL", llamaModelPath);
	applyNonEmptyEnvironment("OFXIC_LLAMA_MODELS", llamaModelDirectory);
	applyNonEmptyEnvironment("OFXIC_SD_SERVER", stableDiffusionServerPath);
	applyNonEmptyEnvironment("OFXIC_SD_MODEL", stableDiffusionModelPath);
	applyNonEmptyEnvironment("OFXIC_SD_MODELS", stableDiffusionModelDirectory);
	applyNonEmptyEnvironment("OFXIC_SD_VAE", stableDiffusionVaePath);
	applyNonEmptyEnvironment("OFXIC_SD_TEXT_ENCODER", stableDiffusionTextEncoderPath);
	applyNonEmptyEnvironment("OFXIC_SD_CLIP_L", stableDiffusionClipLPath);
	applyNonEmptyEnvironment("OFXIC_SD_CLIP_G", stableDiffusionClipGPath);
	const std::string completeCheckpoint = environmentValue("OFXIC_SD_COMPLETE_CHECKPOINT");
	if (!completeCheckpoint.empty()) stableDiffusionCompleteCheckpoint = completeCheckpoint == "1";
	applyNonEmptyEnvironment("OFXIC_ACESTEP_SERVER", aceStepServerPath);
	applyNonEmptyEnvironment("OFXIC_ACESTEP_SERVER_ARGS", aceStepServerArguments);
	applyNonEmptyEnvironment("OFXIC_ACESTEP_MODELS", aceStepModelDirectory);
	applyNonEmptyEnvironment("OFXIC_WHISPER_SERVER", whisperServerPath);
	applyNonEmptyEnvironment("OFXIC_WHISPER_MODEL", whisperModelPath);
	applyNonEmptyEnvironment("OFXIC_WHISPER_SERVER_ARGS", whisperServerArguments);
	applyNonEmptyEnvironment("OFXIC_SAM_BRIDGE_EXECUTABLE", samBridgeExecutablePath);
	applyNonEmptyEnvironment("OFXIC_SAM_BRIDGE_ARGS", samBridgeArguments);
	applyNonEmptyEnvironment("OFXIC_SAM_RUNNER", samRunnerPath);
	applyNonEmptyEnvironment("OFXIC_SAM_MODEL", samModelPath);
}

const char * musicJobStateLabel(ofxIC::StabilityAudioJobState state) {
	switch (state) {
	case ofxIC::StabilityAudioJobState::Submitted: return "submitted";
	case ofxIC::StabilityAudioJobState::Generating: return "generating";
	case ofxIC::StabilityAudioJobState::Completed: return "completed";
	case ofxIC::StabilityAudioJobState::Failed: return "failed";
	default: return "unknown";
	}
}

const char * musicJobStateLabel(ofxIC::AceStepMusicJobState state) {
	switch (state) {
	case ofxIC::AceStepMusicJobState::Submitted: return "submitted";
	case ofxIC::AceStepMusicJobState::Generating: return "generating";
	case ofxIC::AceStepMusicJobState::Completed: return "completed";
	case ofxIC::AceStepMusicJobState::Failed: return "failed";
	default: return "unknown";
	}
}

ofApp::~ofApp() {
	cancellationRequested = true;
	finishWorker();
	finishMediaWorker();
	stopLocalLlamaServer();
	stopLocalStableDiffusionServer();
	stopLocalAceStepServer();
	stopLocalWhisperServer();
	stopLocalSamBridge();
	tokenInput.fill('\0');
	mediaTokenInput.fill('\0');
	musicTokenInput.fill('\0');
	for (auto & entry : storedTokens) {
		std::fill(entry.second.begin(), entry.second.end(), '\0');
	}
	storedTokens.clear();
}

void ofApp::setup() {
	const auto healthProbe = [](unsigned short port) {
		return [port]() {
		ofHttpRequest request("http://127.0.0.1:" + std::to_string(port) + "/health", "runtime-readiness");
		request.timeoutSeconds = 2;
		ofURLFileLoader loader;
		const auto response = loader.handleRequest(request);
		if (response.status != 200) return false;
		const auto body = ofJson::parse(response.data.getText(), nullptr, false);
		if (port == 8085 && body.is_object() && body.contains("data")) {
			const auto & data = body["data"];
			return data.is_object() && data.contains("status") && data["status"] == "ok"
				&& data.contains("models_initialized") && data["models_initialized"] == true;
		}
		return body.is_object() && body.contains("status") && body["status"] == "ok";
		};
	};
	llamaProcess.setReadinessProbe(healthProbe(8080));
	whisperProcess.setReadinessProbe(healthProbe(8082));
	aceStepProcess.setReadinessProbe(healthProbe(8085));
	stableDiffusionProcess.setReadinessProbe([]() {
		ofxIC::Endpoint endpoint("http://127.0.0.1:8081");
		ofxIC::MediaClient client(endpoint);
		ofxIC::RequestControl control;
		control.timeoutSeconds = 2;
		return static_cast<bool>(client.inspectCapabilities(control));
	});
	samBridgeProcess.setReadinessProbe([]() {
		ofxIC::Endpoint endpoint("http://127.0.0.1:18085");
		ofxIC::SegmentationClient client(endpoint);
		ofxIC::RequestControl control;
		control.timeoutSeconds = 2;
		return static_cast<bool>(client.inspectSamBridge(control));
	});
	ofDisableArbTex();
	ofSetWindowTitle("ofxIC Endpoint Workbench");
	ofSetBackgroundColor(20);
	ofLogNotice("ofxIC build") << "Workbench compiled " << __DATE__ << " " <<
		__TIME__ << "; executable=" <<
		(std::filesystem::path(ofFilePath::getCurrentExeDir()) / "ofxICExample.exe").string();
	gui.setup(nullptr, true);
	guiHeartbeatPath = environmentValue("OFXIC_GUI_HEARTBEAT_PATH");
	streamChat = environmentValue("OFXIC_CHAT_STREAM") == "1";
	chat.setSystemPrompt(chatSystemPrompt.data());
	ofxIC::ChatOptions options;
	options.model = modelId.data();
	options.maxTokens = std::clamp(chatMaxTokens, 1, 131072);
	options.temperature = std::clamp(chatTemperature, 0.0f, 2.0f);
	options.topP = std::clamp(chatTopP, 0.0f, 1.0f);
	options.seed = chatSeed;
	options.stopSequences = nonEmptyLines(chatStopSequences.data());
	chat.setOptions(options);
	const std::string architectureDocument =
		"ofxIC keeps llama-server, ggml, CUDA, and model runtimes outside "
		"the addon behind an HTTP process boundary. The addon provides endpoint "
		"access, chat history, explicit document search, and allowlisted tools.";
	if (documents.addText("architecture.md", architectureDocument)) {
		loadedDocumentSources.push_back("architecture.md");
		loadedDocumentContents.push_back(architectureDocument);
	}
	tools.addDocumentSearch(documents);
	documentStatus = "Drop a .md or .txt file here, or choose one explicitly.";
	const std::string documentPath = environmentValue("OFXIC_DOCUMENT_PATH");
	if (!documentPath.empty()) loadDocument(documentPath);
	status = "Ready. Inspect the endpoint, then send a message.";
	if (environmentValue("OFXIC_INSPECT_AUTORUN") == "1") {
		const int cancelAfterMillis = ofToInt(
			environmentValue("OFXIC_INSPECT_CANCEL_AFTER_MS"));
		if (cancelAfterMillis > 0) {
			automationCancelAtMillis = ofGetElapsedTimeMillis() +
				static_cast<std::uint64_t>(cancelAfterMillis);
		}
		pendingInspectAutorun = true;
	}
	const std::string chatAutorun = environmentValue("OFXIC_CHAT_AUTORUN");
	if (!chatAutorun.empty()) {
		pendingChatAutorun = chatAutorun;
	}
	const std::string transcriptionAutorun = environmentValue("OFXIC_TRANSCRIPTION_AUTORUN");
	if (transcriptionAutorun == "openai" || transcriptionAutorun == "whisper-cpp") {
		transcriptionProtocol = transcriptionAutorun == "whisper-cpp" ? 1 : 0;
		const std::string audioPath = environmentValue("OFXIC_AUDIO_PATH");
		if (loadAudio(audioPath)) {
			transcribeAudio();
		} else {
			writeAutomationResult(audioStatus, "");
		}
	}
	if (environmentValue("OFXIC_SEGMENTATION_AUTORUN") == "1") {
		if (loadSegmentationImage(environmentValue("OFXIC_SEGMENTATION_IMAGE"))) {
			const std::string pointX = environmentValue("OFXIC_SEGMENTATION_POINT_X");
			const std::string pointY = environmentValue("OFXIC_SEGMENTATION_POINT_Y");
			if (!pointX.empty()) segmentationPointX = ofClamp(ofToFloat(pointX), 0.0f, 1.0f);
			if (!pointY.empty()) segmentationPointY = ofClamp(ofToFloat(pointY), 0.0f, 1.0f);
			const std::string negativeX = environmentValue("OFXIC_SEGMENTATION_NEGATIVE_POINT_X");
			const std::string negativeY = environmentValue("OFXIC_SEGMENTATION_NEGATIVE_POINT_Y");
			if (!negativeX.empty() && !negativeY.empty()) {
				segmentationPoints.push_back({ segmentationPointX, segmentationPointY, true });
				segmentationPoints.push_back({
					ofClamp(ofToFloat(negativeX), 0.0f, 1.0f),
					ofClamp(ofToFloat(negativeY), 0.0f, 1.0f), false });
			}
			segmentImage();
		} else {
			writeAutomationResult(segmentationStatus, "");
		}
	}
	setTextBuffer(mediaInput, "A small paper sculpture on a clean studio background");
	mediaStatus = "Choose OpenAI images, Hugging Face / fal-ai, or stable-diffusion.cpp jobs.";
	setTextBuffer(musicInput,
		"Warm evolving modular synthesizer, gentle pulse, instrumental, no vocals");
	musicStatus = selectedMusicBackend == 0
		? "ACE-Step music runs through the local external server at port 8085."
		: "Stable Audio 3 runs as an asynchronous external Stability AI job.";
	const std::string musicAutorun = environmentValue("OFXIC_MUSIC_AUTORUN");
	if (musicAutorun == "acestep" || musicAutorun == "stability") {
		const int autorunBackend = musicAutorun == "acestep" ? 0 : 1;
		selectedMusicBackend = autorunBackend;
		if (environmentValue("OFXIC_MUSIC_ENDPOINT_URL").empty()) {
			setTextBuffer(musicEndpointUrl, musicBackends[selectedMusicBackend].url);
		}
		applyMusicConfiguration();
		const std::string prompt = environmentValue("OFXIC_MUSIC_PROMPT");
		if (!prompt.empty()) setTextBuffer(musicInput, prompt);
		generateMusic();
	}
	const std::string mediaAutorun = environmentValue("OFXIC_MEDIA_AUTORUN");
	if (mediaAutorun == "image" || mediaAutorun == "video") {
		selectedMediaKind = mediaAutorun == "video" ? 1 : 0;
		const std::string prompt = environmentValue("OFXIC_MEDIA_PROMPT");
		if (!prompt.empty()) setTextBuffer(mediaInput, prompt);
		if (supportsMediaKind(selectedMediaBackend, selectedMediaKind)) {
			generateMedia();
		} else {
			mediaStatus = unsupportedMediaMessage(selectedMediaBackend, selectedMediaKind);
			writeMediaAutomationResult(mediaStatus, "");
		}
	}
	const std::string diagnosticsPath = environmentValue("OFXIC_DIAGNOSTICS_PATH");
	if (!diagnosticsPath.empty()) exportDiagnostics(diagnosticsPath);
	configureRuntimeAutomation();
}

void ofApp::update() {
	if (mediaBusy && !guiHeartbeatPath.empty()) {
		++guiHeartbeatFrames;
		const std::uint64_t now = ofGetElapsedTimeMillis();
		if (now - guiHeartbeatLastWriteMillis >= 100) {
			std::ofstream heartbeat(guiHeartbeatPath, std::ios::binary | std::ios::trunc);
			if (heartbeat) heartbeat << guiHeartbeatFrames << '\n';
			guiHeartbeatLastWriteMillis = now;
		}
	}
	updateLocalLlamaServer();
	updateManagedProcess(stableDiffusionProcess, stableDiffusionServerStatus, "sd-server");
	updateManagedProcess(aceStepProcess, aceStepServerStatus, "ACE-Step server");
	updateManagedProcess(whisperProcess, whisperServerStatus, "whisper.cpp server");
	updateManagedProcess(samBridgeProcess, samBridgeProcessStatus, "SAM bridge");
	updateRuntimeAutomation();
	continueDeferredTask();
	if (pendingInspectAutorun && !busy) {
		pendingInspectAutorun = false;
		inspectEndpoint();
	}
	if (!pendingChatAutorun.empty() && !busy) {
		setTextBuffer(input, pendingChatAutorun);
		pendingChatAutorun.clear();
		sendMessage();
	}
	if (automationCancelAtMillis > 0 && busy && requestCanCancel &&
		ofGetElapsedTimeMillis() >= automationCancelAtMillis) {
		automationCancelAtMillis = 0;
		cancelRequest();
	}
	if (busy && requestCanCancel) {
		std::lock_guard<std::mutex> lock(resultMutex);
		if (!pendingProgressStatus.empty()) status = pendingProgressStatus;
		if (!pendingStreamOutput.empty() && output != pendingStreamOutput) {
			output = pendingStreamOutput;
			writeStreamingAutomationResult(output);
		}
	}
	if (finished.exchange(false)) {
		finishWorker();
		busy = false;
		requestCanCancel = false;
		std::lock_guard<std::mutex> lock(resultMutex);
		output = std::move(pendingOutput);
		status = std::move(pendingStatus);
		if (status.rfind("Segmentation", 0) == 0) {
			segmentationStatus = status;
			if (!pendingSegmentationMask.empty()) {
				ofPixels maskPixels;
				const ofBuffer maskBuffer(
					pendingSegmentationMask.data(), pendingSegmentationMask.size());
				if (ofLoadImage(maskPixels, maskBuffer)) {
					segmentationMaskImage.setFromPixels(maskPixels);
				} else {
					segmentationStatus = "Segmentation returned an unreadable PGM mask";
				}
				pendingSegmentationMask.clear();
			}
		}
		availableModels = std::move(pendingModels);
		if (!pendingModelSelection.empty()) {
			setTextBuffer(modelId, pendingModelSelection);
			ofxIC::ChatOptions options = chat.getOptions();
			options.model = pendingModelSelection;
			chat.setOptions(options);
			pendingModelSelection.clear();
		}
		writeAutomationResult(status, output);
		if (!activeTaskKind.empty()) {
			recordTaskHistory(activeTaskKind, status);
			activeTaskKind.clear();
		}
	}
	if (mediaBusy) {
		std::lock_guard<std::mutex> lock(mediaResultMutex);
		if (!pendingMediaProgressStatus.empty())
			mediaStatus = pendingMediaProgressStatus;
	}
	if (mediaFinished.exchange(false)) {
		finishMediaWorker();
		mediaBusy = false;
		std::string savedPath;
		bool isVideo = false;
		bool receivedCapabilities = false;
		{
			std::lock_guard<std::mutex> lock(mediaResultMutex);
			mediaStatus = std::move(pendingMediaStatus);
			pendingMediaProgressStatus.clear();
			mediaOutput = std::move(pendingMediaOutput);
			currentMediaJob = std::move(pendingMediaJob);
			savedPath = std::move(pendingMediaSavedPath);
			isVideo = pendingMediaIsVideo;
			pendingMediaIsVideo = false;
			if (pendingMediaCapabilitiesReady) {
				currentMediaCapabilities = std::move(pendingMediaCapabilities);
				pendingMediaCapabilitiesReady = false;
				receivedCapabilities = true;
			}
		}
		if (receivedCapabilities && currentMediaCapabilities) {
			ofxICExample::MediaControlSelection selection{
				selectedMediaKind, mediaSampler, mediaScheduler, mediaOutputFormat };
			ofxICExample::reconcileMediaControls(currentMediaCapabilities,
				ofxICExample::mediaModelMatches(currentMediaCapabilities.model,
					stableDiffusionModelPath.data()), selection);
			selectedMediaKind = selection.kind;
			mediaSampler = std::move(selection.sampler);
			mediaScheduler = std::move(selection.scheduler);
			mediaOutputFormat = std::move(selection.outputFormat);
		}
		if (!savedPath.empty()) {
				if (isVideo) {
					generatedImage.clear();
					generatedVideo.close();
					if (generatedVideo.load(savedPath)) {
						generatedVideo.setLoopState(OF_LOOP_NORMAL);
						generatedVideo.setSpeed(1.0f);
						generatedVideo.setPosition(0.0f);
						generatedVideo.play();
						generatedVideo.setPaused(false);
						mediaOutput += "\nPreview loaded with " +
							ofToString(currentMediaJob.frameCount) + " frames at " +
							ofToString(currentMediaJob.fps) + " FPS.";
					} else {
						mediaOutput += "\nThe video was generated and saved, but the local player could not decode this container.";
					}
				} else {
					generatedVideo.close();
					generatedImage.load(savedPath);
				}
		}
		writeMediaAutomationResult(mediaStatus, mediaOutput);
		if (!activeMediaTaskKind.empty()) {
			recordTaskHistory(activeMediaTaskKind, mediaStatus, savedPath);
			activeMediaTaskKind.clear();
		}
	}
	if (musicFinished.exchange(false)) {
		finishMediaWorker();
		mediaBusy = false;
		std::string bytes;
		std::string format;
		{
			std::lock_guard<std::mutex> lock(mediaResultMutex);
			musicStatus = std::move(pendingMusicStatus);
			musicOutput = std::move(pendingMusicOutput);
			currentMusicJob = std::move(pendingMusicJob);
			currentAceStepMusicJob = std::move(pendingAceStepMusicJob);
			bytes = std::move(pendingMusicBytes);
			format = std::move(pendingMusicFormat);
		}
		std::string savedPath;
		if (!bytes.empty()) {
			const std::string extension = format == "wav" ? "wav" : "mp3";
			const std::string path = ofToDataPath(
				timestampedOutputFilename("music", extension), true);
			if (ofBufferToFile(path, ofBuffer(bytes.data(), bytes.size()))) {
				savedPath = path;
				generatedMusic.stop();
				if (generatedMusic.load(path)) {
					generatedMusic.play();
					musicOutput += "\nSaved and playing: " + path;
				} else {
					musicOutput += "\nSaved, but playback could not load: " + path;
				}
			} else {
				musicOutput += "\nCould not save generated audio.";
			}
		}
		writeMusicAutomationResult(musicStatus, musicOutput);
		if (!activeMusicTaskKind.empty()) {
			recordTaskHistory(activeMusicTaskKind, musicStatus, savedPath);
			activeMusicTaskKind.clear();
		}
	}
	generatedVideo.update();
}

void ofApp::draw() {
	bool applyRequested = false;
	bool inspectRequested = false;
	bool sendRequested = false;
	bool cancelRequested = false;
	bool cancelDeferredRequested = false;
	bool clearHistoryRequested = false;
	bool exportDiagnosticsRequested = false;
	const bool taskLocked = busy || mediaBusy || deferredTask != DeferredTask::None;
	bool saveTokenRequested = false;
	bool forgetTokenRequested = false;
	bool saveMediaTokenRequested = false;
	bool forgetMediaTokenRequested = false;
	bool clearRequested = false;
	bool loadDocumentRequested = false;
	bool loadAudioRequested = false;
	bool transcribeAudioRequested = false;
	bool loadSegmentationImageRequested = false;
	bool inspectSegmentationBridgeRequested = false;
	bool segmentImageRequested = false;
	bool generateMediaRequested = false;
	bool inspectMediaContextRequested = false;
	bool applyMediaDefaultsRequested = false;
	bool generateMusicRequested = false;
	bool saveMusicTokenRequested = false;
	bool forgetMusicTokenRequested = false;
	bool saveSettingsRequested = false;
	bool resetSettingsRequested = false;
	bool loadLlamaModelRequested = false;
	bool chooseLlamaModelDirectoryRequested = false;
	bool rescanLlamaModelsRequested = false;
	bool chooseLlamaServerRequested = false;
	bool startLlamaServerRequested = false;
	bool stopLlamaServerRequested = false;
	bool chooseSdServerRequested = false;
	bool chooseSdModelDirectoryRequested = false;
	bool rescanSdModelsRequested = false;
	bool chooseSdModelRequested = false;
	bool chooseSdVaeRequested = false;
	bool chooseSdTextEncoderRequested = false;
	bool chooseSdClipLRequested = false;
	bool chooseSdClipGRequested = false;
	bool startSdServerRequested = false;
	bool stopSdServerRequested = false;
	bool chooseAceStepServerRequested = false;
	bool chooseAceStepModelDirectoryRequested = false;
	bool startAceStepServerRequested = false;
	bool stopAceStepServerRequested = false;
	bool chooseWhisperServerRequested = false;
	bool chooseWhisperModelRequested = false;
	bool startWhisperServerRequested = false;
	bool stopWhisperServerRequested = false;
	bool chooseSamBridgeRequested = false;
	bool chooseSamRunnerRequested = false;
	bool chooseSamModelRequested = false;
	bool startSamBridgeRequested = false;
	bool stopSamBridgeRequested = false;
	bool rescanInstalledRuntimesRequested = false;

	gui.begin();
	ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(1168, 658), ImGuiCond_FirstUseEver);
	ImGui::Begin("Inference");
	const auto fitMediaPreview = [](float width, float height) {
		if (width <= 0.0f || height <= 0.0f) return ImVec2(1.0f, 1.0f);
		const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		constexpr float maximumPreviewHeight = 320.0f;
		const float scale = std::min(1.0f, std::min(
			availableWidth / width,
			maximumPreviewHeight / height));
		return ImVec2(width * scale, height * scale);
	};
	const auto runtimeStateColor = [](ofxICExample::ManagedProcessState state) {
		switch (state) {
		case ofxICExample::ManagedProcessState::Ready:
			return ImVec4(0.24f, 0.82f, 0.45f, 1.0f);
		case ofxICExample::ManagedProcessState::Starting:
			return ImVec4(0.95f, 0.72f, 0.22f, 1.0f);
		case ofxICExample::ManagedProcessState::Exited:
			return ImVec4(0.95f, 0.52f, 0.22f, 1.0f);
		case ofxICExample::ManagedProcessState::Failed:
			return ImVec4(0.95f, 0.28f, 0.28f, 1.0f);
		default:
			return ImVec4(0.60f, 0.63f, 0.68f, 1.0f);
		}
	};
	const auto drawRuntimeStatus = [&](ofxICExample::ManagedProcess & process,
		const std::string & statusText, const char * id) {
		ImGui::SameLine();
		ImGui::TextColored(runtimeStateColor(process.state()), "[%s]",
			ofxICExample::managedProcessStateLabel(process.state()));
		ImGui::TextWrapped("%s", statusText.c_str());
		const auto & followedFiles = process.followedOutputFiles();
		if (!followedFiles.empty()) {
			ImGui::TextDisabled("External log source%s:", followedFiles.size() == 1 ? "" : "s");
			for (const std::string & path : followedFiles) {
				ImGui::TextWrapped("%s", path.c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("(%s)", logFileAge(path).c_str());
			}
		}
		const std::string & log = process.recentOutput();
		if (log.empty()) return;
		const std::string header = std::string("Server output##") + id;
		if (process.state() == ofxICExample::ManagedProcessState::Failed ||
			process.state() == ofxICExample::ManagedProcessState::Exited)
			ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
		if (ImGui::CollapsingHeader(header.c_str())) {
			const std::string clear = std::string("Clear view##server-output-clear-") + id;
			if (ImGui::Button(clear.c_str())) process.clearRecentOutput();
			const std::string child = std::string("server-output-child##") + id;
			ImGui::BeginChild(child.c_str(), ImVec2(-1, 120), true,
				ImGuiWindowFlags_HorizontalScrollbar);
			ImGui::TextUnformatted(log.data(), log.data() + log.size());
			if (process.running()) ImGui::SetScrollHereY(1.0f);
			ImGui::EndChild();
		}
	};
	const auto drawRuntimeControls = [&](ofxICExample::ManagedProcess & process,
		const std::string & statusText, const char * id,
		bool & startRequested, bool & stopRequested) {
		const bool running = process.running();
		const std::string startId = std::string("Start##runtime-") + id;
		ImGui::BeginDisabled(running);
		if (ImGui::Button(startId.c_str())) startRequested = true;
		ImGui::EndDisabled();
		ImGui::SameLine();
		const std::string stopLabel = process.ownsProcess() ? "Stop" : "Disconnect";
		const std::string stopId = stopLabel + "##runtime-" + id;
		ImGui::BeginDisabled(!running);
		if (ImGui::Button(stopId.c_str())) stopRequested = true;
		ImGui::EndDisabled();
		drawRuntimeStatus(process, statusText, id);
	};
	const auto drawEndpointInput = [&](const char * label, auto & destination,
		const char * id, bool * dirty = nullptr) {
		if (ImGui::InputText(label, destination.data(), destination.size()) && dirty)
			*dirty = true;
		ImGui::SameLine();
		const std::string buttonId = std::string("Use LLM URL##endpoint-") + id;
		if (ImGui::Button(buttonId.c_str())) {
			setTextBuffer(destination, endpointUrl.data());
			if (dirty) *dirty = true;
		}
	};
	const auto drawPathInput = [](const char * label, auto & destination,
		const char * id, bool & chooseRequested) {
		ImGui::InputText(label, destination.data(), destination.size());
		ImGui::SameLine();
		const std::string buttonId = std::string("Choose...##path-") + id;
		if (ImGui::Button(buttonId.c_str())) chooseRequested = true;
	};
	const auto drawRuntimeExecutable = [&](const char * label, auto & destination,
		std::string & detected, const char * id, const char * familyPrefix,
		const char * executableName, const char * installerScript,
		std::string & statusText, bool & chooseRequested) {
		ImGui::InputText(label, destination.data(), destination.size());
		ImGui::SameLine();
		const std::string chooseId = std::string("Choose...##runtime-executable-") + id;
		if (ImGui::Button(chooseId.c_str())) chooseRequested = true;
		ImGui::SameLine();
		const std::string detectId = std::string("Detect installed##runtime-executable-") + id;
		if (ImGui::Button(detectId.c_str())) {
			detected = installedServerExecutable(familyPrefix, executableName);
			if (detected.empty()) {
				statusText = std::string("No installed ") + executableName +
					" found. Run scripts\\" + installerScript + ".";
			} else {
				setTextBuffer(destination, detected);
				statusText = std::string("Installed ") + executableName + " selected.";
			}
			ofLogNotice("ofxIC servers") << "Detect " << executableName << ": " <<
				(detected.empty() ? "not found" : detected);
		}
	};
	const auto drawAuthentication = [&](const char * id, const std::string & token,
		const std::string & source, const std::string & environment, auto & input,
		bool & saveRequested, bool & forgetRequested) {
		const std::string header = std::string("Authentication - ") +
			(token.empty() ? "not loaded##auth-" : "loaded##auth-") + id;
		if (!ImGui::CollapsingHeader(header.c_str())) return;
		ImGui::TextDisabled("Source: %s", source.c_str());
		if (token.empty())
			ImGui::TextWrapped("Set without storing it: %s",
				tokenSetupHint(environment).c_str());
		if (!ofxICExample::credentialStoreAvailable()) return;
		ImGui::SetNextItemWidth(280);
		const std::string inputId = std::string("Token##auth-input-") + id;
		ImGui::InputText(inputId.c_str(), input.data(), input.size(),
			ImGuiInputTextFlags_Password);
		ImGui::SameLine();
		ImGui::BeginDisabled(taskLocked || !input[0]);
		const std::string saveId = std::string("Save securely##auth-save-") + id;
		if (ImGui::Button(saveId.c_str())) saveRequested = true;
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(taskLocked || storedTokens.count(environment) == 0);
		const std::string forgetId = std::string("Forget saved token##auth-forget-") + id;
		if (ImGui::Button(forgetId.c_str())) forgetRequested = true;
		ImGui::EndDisabled();
	};
	if (ImGui::BeginTabBar("Inference task tabs")) {
	if (ImGui::BeginTabItem("Overview")) {
		ImGui::Text("ofxIC %s", ofxIC::versionString);
		if (ImGui::CollapsingHeader("Diagnostics")) {
			if (ImGui::Button("Export diagnostics...")) exportDiagnosticsRequested = true;
			ImGui::TextDisabled("%s", diagnosticsStatus.c_str());
			ImGui::TextDisabled("Workbench build: %s %s", __DATE__, __TIME__);
			ImGui::TextWrapped("%s", (std::filesystem::path(ofFilePath::getCurrentExeDir()) /
				"ofxICExample.exe").string().c_str());
		}
		ImGui::SeparatorText("Local runtimes");
		ImGui::TextWrapped(
			"External runtimes stay separate from ofxIC. Configure models and paths in "
			"their task tabs; supervise all owned processes here.");
		rescanInstalledRuntimesRequested = ImGui::Button("Rescan installed runtimes");
		ImGui::SameLine();
		ImGui::TextDisabled("%s", installedServerRoot().c_str());
		if (ImGui::BeginTable("runtime-dashboard", 5,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Runtime");
			ImGui::TableSetupColumn("State");
			ImGui::TableSetupColumn("Configuration");
			ImGui::TableSetupColumn("Endpoint");
			ImGui::TableSetupColumn("Action");
			ImGui::TableHeadersRow();
			const auto configuration = [](bool executable, bool model,
				const char * modelLabel = "model") {
				if (executable && model) return std::string("configured");
				if (!executable && !model)
					return std::string("missing executable + ") + modelLabel;
				return executable ? std::string("missing ") + modelLabel
					: std::string("missing executable");
			};
			const auto row = [&](const char * name, const char * endpoint,
				const char * id, const ofxICExample::ManagedProcess & process,
				const std::string & configurationState,
				bool & startRequested, bool & stopRequested) {
				const bool configured = configurationState == "configured";
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name);
				ImGui::TableSetColumnIndex(1);
				ImGui::TextColored(runtimeStateColor(process.state()), "%s",
					ofxICExample::managedProcessStateLabel(process.state()));
				ImGui::TableSetColumnIndex(2);
				ImGui::TextColored(configured
					? ImVec4(0.24f, 0.82f, 0.45f, 1.0f)
					: ImVec4(0.95f, 0.28f, 0.28f, 1.0f),
					"%s", configurationState.c_str());
				ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(endpoint);
				ImGui::TableSetColumnIndex(4);
				const std::string action = process.running()
					? (process.ownsProcess() ? "Stop##" : "Disconnect##") : "Start##";
				const std::string button = action + id;
				if (ImGui::Button(button.c_str())) {
					if (process.running()) stopRequested = true;
					else startRequested = true;
				}
			};
			row("llama-server", "127.0.0.1:8080", "overview-llama", llamaProcess,
				configuration(configuredRuntime(llamaServerPath.data(), detectedLlamaServerPath),
					configuredPath(llamaModelPath.data()), "GGUF model"),
				startLlamaServerRequested, stopLlamaServerRequested);
			row("sd-server", "127.0.0.1:8081", "overview-sd", stableDiffusionProcess,
				configuration(configuredRuntime(stableDiffusionServerPath.data(),
					detectedStableDiffusionServerPath),
					configuredPath(stableDiffusionModelPath.data()), "diffusion model"),
				startSdServerRequested, stopSdServerRequested);
			row("whisper.cpp", "127.0.0.1:8082", "overview-whisper", whisperProcess,
				configuration(configuredRuntime(whisperServerPath.data(), detectedWhisperServerPath),
					configuredPath(whisperModelPath.data()), "Whisper model"),
				startWhisperServerRequested, stopWhisperServerRequested);
			row("ACE-Step", "127.0.0.1:8085", "overview-acestep", aceStepProcess,
				configuration(configuredRuntime(aceStepServerPath.data(), detectedAceStepServerPath),
					(!isNativeAceStepServer(aceStepServerPath.data()) ||
						configuredPath(aceStepModelDirectory.data())), "GGUF folder"),
				startAceStepServerRequested, stopAceStepServerRequested);
			row("SAM bridge", "127.0.0.1:18085", "overview-sam", samBridgeProcess,
				configuration(configuredRuntime(samBridgeExecutablePath.data(), detectedSamPythonPath),
					(configuredPath(samRunnerPath.data()) || !detectedSamRunnerPath.empty()) &&
					configuredPath(samModelPath.data()), "runner/checkpoint"),
				startSamBridgeRequested, stopSamBridgeRequested);
			ImGui::EndTable();
		}
		ImGui::TextDisabled(
			"Configured means paths are set, not that files are accessible or the server is ready.");
		ImGui::TextDisabled(
			"Failed starts remain actionable: open the matching task tab for the exact status and server output.");
		if (ImGui::CollapsingHeader("Runtime setup (Windows)")) {
			ImGui::TextWrapped(
				"Copy a command and run it in PowerShell. Plan shows versions and destinations without "
				"installing; install uses the bundled script's fixed version. Then rescan above. "
				"No downloads or update checks run in the GUI.");
			if (ImGui::BeginTable("runtime-installers", 4,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
				ImGui::TableSetupColumn("Runtime");
				ImGui::TableSetupColumn("Detected package folder");
				ImGui::TableSetupColumn("Policy");
				ImGui::TableSetupColumn("Script command");
				ImGui::TableHeadersRow();
				const auto installerRow = [&](const char * name, const char * id,
					const std::string & installedPath, int parentLevels,
					const char * script, const std::string & arguments, const char * policy) {
					// Resolve scripts only on user action, never on every rendered frame.
					const auto copyCommand = [&](bool plan) {
						const std::string command = runtimeInstallerCommand(script, arguments, plan);
						if (command.empty()) {
							runtimeSetupStatus = std::string("Script not found: ") + script +
								". Use the scripts directory from the addon checkout.";
							return;
						}
						ImGui::SetClipboardText(command.c_str());
						runtimeSetupStatus = std::string(name) +
							(plan ? " plan" : " install") + " command copied:\n" + command;
					};
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name);
					ImGui::TableSetColumnIndex(1);
					const bool installed = !installedPath.empty();
					ImGui::TextColored(installed
						? ImVec4(0.24f, 0.82f, 0.45f, 1.0f)
						: ImVec4(0.95f, 0.72f, 0.22f, 1.0f), "%s",
						runtimeInstallIdentity(installedPath, parentLevels).c_str());
					if (installed && ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", installedPath.c_str());
					ImGui::TableSetColumnIndex(2); ImGui::TextWrapped("%s", policy);
					ImGui::TableSetColumnIndex(3);
					const std::string planButton = std::string("Copy plan##runtime-plan-") + id;
					if (ImGui::SmallButton(planButton.c_str())) copyCommand(true);
					ImGui::SameLine();
					const std::string installButton = std::string("Copy install##runtime-install-") + id;
					if (ImGui::SmallButton(installButton.c_str())) copyCommand(false);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", script);
				};
				installerRow("llama.cpp", "llama", detectedLlamaServerPath, 1,
					"install-llama-server.ps1", "", "Pinned binary + SHA-256");
				installerRow("stable-diffusion.cpp", "sd", detectedStableDiffusionServerPath, 1,
					"install-stable-diffusion-server.ps1", "", "Pinned binary + SHA-256");
				installerRow("whisper.cpp", "whisper", detectedWhisperServerPath, 1,
					"install-whisper-server.ps1", "", "Pinned binary + base model");
				installerRow("ACE-Step", "acestep", detectedAceStepServerPath, 1,
					"install-acestep-server.ps1", "", "Pinned source commit + CUDA build");
				installerRow("SAM", "sam", detectedSamPythonPath, 3,
					"install-sam-server.ps1", samModelPath[0]
						? "-ExistingModel " + powerShellLiteral(samModelPath.data())
						: "-DownloadModel", "Pinned Python/CUDA environment");
				ImGui::EndTable();
			}
			ImGui::TextWrapped("%s", runtimeSetupStatus.c_str());
			ImGui::TextWrapped(
				"Detection is a path snapshot, not a compatibility test. Custom executables remain "
				"selectable in the task tabs. A newer upstream release is not a validated addon update.");
		}
		if (deferredTask != DeferredTask::None) {
			ImGui::SeparatorText("Queued task");
			ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.22f, 1.0f), "%s is waiting for its runtime.",
				deferredTaskLabel(deferredTask));
			ImGui::SameLine();
			cancelDeferredRequested = ImGui::Button("Cancel queued task");
		}
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("LLM")) {
		ImGui::SeparatorText("Endpoint connection");
		ImGui::BeginDisabled(taskLocked);
		if (ImGui::BeginCombo("Endpoint", endpointProfiles[selectedProfile].name)) {
			for (std::size_t index = 0; index < endpointProfiles.size(); ++index) {
				const bool selected = selectedProfile == static_cast<int>(index);
				if (ImGui::Selectable(endpointProfiles[index].name, selected))
					selectEndpointProfile(static_cast<int>(index));
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		if (ImGui::InputText("Base URL", endpointUrl.data(), endpointUrl.size()))
			configurationDirty = true;
		if (selectedProfile == 0) {
			ImGui::TextDisabled("API model: supplied by the running llama-server and discovered by Inspect / models.");
		} else if (ImGui::InputText("Model ID", modelId.data(), modelId.size())) {
			configurationDirty = true;
		}
		if (selectedProfile != 0 && !availableModels.empty()) {
			ImGui::SameLine();
			if (ImGui::BeginCombo("Available", modelId[0] ? modelId.data() : "select model")) {
				for (const auto & model : availableModels) {
					if (ImGui::Selectable(model.c_str(), model == modelId.data())) {
						setTextBuffer(modelId, model);
						configurationDirty = true;
					}
				}
				ImGui::EndCombo();
			}
		}
		applyRequested = ImGui::Button(configurationDirty ? "Apply *" : "Apply");
		ImGui::SameLine(); inspectRequested = ImGui::Button("Inspect / models");
		ImGui::SameLine(); saveSettingsRequested = ImGui::Button("Save settings");
		ImGui::SameLine(); resetSettingsRequested = ImGui::Button("Reset saved settings");
		ImGui::EndDisabled();
		const std::string token = configuredToken();
		const std::string tokenSource = configuredTokenSource();
		drawAuthentication("llm", token, tokenSource,
			endpointProfiles[selectedProfile].tokenEnvironment, tokenInput,
			saveTokenRequested, forgetTokenRequested);
		if (!credentialStatus.empty()) ImGui::TextWrapped("%s", credentialStatus.c_str());
		ImGui::TextWrapped("%s", status.c_str());
		ImGui::TextDisabled("%s Tokens are never stored in settings.", settingsStatus.c_str());
		if (ImGui::CollapsingHeader("Chat settings")) {
			if (ImGui::InputTextMultiline("System prompt", chatSystemPrompt.data(),
				chatSystemPrompt.size(), ImVec2(-1, 72))) configurationDirty = true;
			if (ImGui::InputInt("Max tokens", &chatMaxTokens)) configurationDirty = true;
			if (ImGui::SliderFloat("Temperature", &chatTemperature, 0.0f, 2.0f, "%.2f"))
				configurationDirty = true;
			if (ImGui::SliderFloat("Top-p", &chatTopP, 0.0f, 1.0f, "%.2f"))
				configurationDirty = true;
			if (ImGui::InputInt("Seed (-1 = random)", &chatSeed)) configurationDirty = true;
			if (ImGui::InputTextMultiline("Stop sequences (one per line)",
				chatStopSequences.data(), chatStopSequences.size(), ImVec2(-1, 54)))
				configurationDirty = true;
			if (ImGui::Button(configurationDirty ? "Apply chat settings *" : "Apply chat settings"))
				applyRequested = true;
		}
		if (selectedProfile == 0) {
			drawRuntimeControls(llamaProcess, llamaServerStatus, "llama",
				startLlamaServerRequested, stopLlamaServerRequested);
			if (ImGui::CollapsingHeader("Runtime settings##llama")) {
			drawRuntimeExecutable("Server executable", llamaServerPath,
				detectedLlamaServerPath, "llama", "llama.cpp-", "llama-server.exe",
				"install-llama-server.ps1", llamaServerStatus,
				chooseLlamaServerRequested);
			drawPathInput("Selected GGUF model", llamaModelPath, "llama-model",
				loadLlamaModelRequested);
			drawPathInput("Model search folder", llamaModelDirectory, "llama-model-folder",
				chooseLlamaModelDirectoryRequested);
			ImGui::SameLine();
			rescanLlamaModelsRequested = ImGui::Button("Rescan models");
			if (!detectedLlamaModels.empty() && ImGui::BeginCombo("Detected GGUF models",
				llamaModelPath[0] ? ofFilePath::getFileName(llamaModelPath.data()).c_str() : "select model")) {
				for (const std::string & model : detectedLlamaModels) {
					const bool selected = model == llamaModelPath.data();
					if (ImGui::Selectable(ofFilePath::getFileName(model).c_str(), selected))
						setTextBuffer(llamaModelPath, model);
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::InputInt("Context size", &llamaContextSize);
			ImGui::SameLine();
			ImGui::InputInt("GPU layers", &llamaGpuLayers);
			ImGui::Checkbox("Flash Attention##llama", &llamaFlashAttention);
			}
		}
		if (!lastMessage.empty()) ImGui::TextDisabled("Last message: %s", lastMessage.c_str());
		ImGui::TextUnformatted("Message");
		if (focusMessageInput && !busy) {
			ImGui::SetKeyboardFocusHere();
			focusMessageInput = false;
		}
		ImGui::BeginDisabled(busy);
		sendRequested = ImGui::InputTextMultiline("##message", input.data(), input.size(),
			ImVec2(-1, 76), ImGuiInputTextFlags_EnterReturnsTrue |
			ImGuiInputTextFlags_CtrlEnterForNewLine);
		if (ImGui::Button("Send")) sendRequested = true;
		ImGui::SameLine();
		clearRequested = ImGui::Button("Clear conversation");
		ImGui::SameLine();
		ImGui::TextDisabled("Enter sends; Ctrl+Enter adds a line");
		ImGui::Checkbox("Stream direct chat", &streamChat);
		if (streamChat) {
			ImGui::SameLine();
			ImGui::TextDisabled("Document tools are disabled for this request");
		}
		ImGui::EndDisabled();
		if (busy) {
			ImGui::SameLine();
			if (requestCanCancel) {
				cancelRequested = ImGui::Button("Cancel request");
				ImGui::SameLine();
				ImGui::TextDisabled(cancellationRequested ? "Cancelling..." : "Waiting for endpoint...");
			} else {
				ImGui::TextDisabled("Waiting for endpoint...");
			}
		}
		ImGui::SeparatorText("Response");
		ImGui::BeginChild("response", ImVec2(0, 0), true);
		ImGui::TextWrapped("%s", output.empty() ? "No response yet." : output.c_str());
		ImGui::EndChild();
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("Documents")) {
		loadDocumentRequested = ImGui::Button("Load .md / .txt");
		ImGui::SameLine();
		ImGui::TextDisabled("%zu document(s), %zu chunk(s)",
			documents.documentCount(), documents.chunkCount());
		ImGui::TextWrapped("%s", documentStatus.c_str());
		ImGui::SeparatorText("Loaded sources");
		if (loadedDocumentSources.empty()) ImGui::TextDisabled("No sources loaded.");
		for (std::size_t index = 0; index < loadedDocumentSources.size(); ++index) {
			if (ImGui::Selectable(loadedDocumentSources[index].c_str(),
				selectedDocument == static_cast<int>(index))) {
				selectedDocument = static_cast<int>(index);
			}
		}
		if (!loadedDocumentContents.empty()) {
			selectedDocument = std::clamp(selectedDocument, 0,
				static_cast<int>(loadedDocumentContents.size() - 1));
			ImGui::SeparatorText("Content preview");
			ImGui::TextDisabled("%s", loadedDocumentSources[selectedDocument].c_str());
			ImGui::BeginChild("document-content", ImVec2(0, 210), true,
				ImGuiWindowFlags_HorizontalScrollbar);
			ImGui::TextUnformatted(loadedDocumentContents[selectedDocument].c_str());
			ImGui::EndChild();
		}
		ImGui::TextWrapped("Chat uses search_documents for questions grounded in these sources.");
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("Transcription")) {
		const char * transcriptionProtocols[] = {
			"OpenAI /v1/audio/transcriptions", "whisper.cpp /inference" };
		ImGui::BeginDisabled(taskLocked);
		if (ImGui::Combo("Protocol", &transcriptionProtocol, transcriptionProtocols, 2)) {
			setTextBuffer(transcriptionEndpointUrl,
				ofxICExample::defaultTranscriptionEndpointUrl(transcriptionProtocol));
		}
		drawEndpointInput("Audio base URL", transcriptionEndpointUrl, "audio");
		ImGui::InputText("Audio model", transcriptionModel.data(), transcriptionModel.size());
		if (transcriptionProtocol == 1) {
			ImGui::TextDisabled("whisper.cpp selects its model when the server starts.");
			drawRuntimeControls(whisperProcess, whisperServerStatus, "whisper",
				startWhisperServerRequested, stopWhisperServerRequested);
			if (ImGui::CollapsingHeader("Runtime settings##whisper")) {
				drawRuntimeExecutable("Server executable", whisperServerPath,
					detectedWhisperServerPath, "whisper", "whisper.cpp-", "whisper-server.exe",
					"install-whisper-server.ps1", whisperServerStatus,
					chooseWhisperServerRequested);
				drawPathInput("Whisper model", whisperModelPath, "whisper-model",
					chooseWhisperModelRequested);
				ImGui::InputText("Extra arguments##whisper", whisperServerArguments.data(), whisperServerArguments.size());
			}
		}
		loadAudioRequested = ImGui::Button("Load audio");
		ImGui::SameLine();
		ImGui::BeginDisabled(audioBytes.empty());
		transcribeAudioRequested = ImGui::Button("Transcribe");
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextDisabled("%s", audioFilename.empty() ? "no file" : audioFilename.c_str());
		ImGui::TextWrapped("%s", audioStatus.c_str());
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("Image / video")) {
	const char * backendNames[] = { "OpenAI images", "Hugging Face / fal-ai", "stable-diffusion.cpp" };
	const char * mediaKinds[] = { "Image", "Video" };
	ImGui::BeginDisabled(taskLocked);
	int nextMediaBackend = selectedMediaBackend;
	if (ImGui::Combo("Media backend", &nextMediaBackend, backendNames, 3)) {
		selectMediaBackend(nextMediaBackend);
	}
	if (selectedMediaBackend != 2 && stableDiffusionServerPath[0]) {
		ImGui::SameLine();
		if (ImGui::Button("Use local sd-server")) selectMediaBackend(2);
		ImGui::TextDisabled("A script-installed sd-server and local checkpoint are available.");
	}
	const MediaBackendProfile & mediaProfile = mediaBackends[selectedMediaBackend];
	if (selectedMediaBackend == 2) {
		drawRuntimeControls(stableDiffusionProcess, stableDiffusionServerStatus, "sd",
			startSdServerRequested, stopSdServerRequested);
		if (ImGui::CollapsingHeader("Runtime settings##sd")) {
		drawRuntimeExecutable("Server executable", stableDiffusionServerPath,
			detectedStableDiffusionServerPath, "sd", "stable-diffusion.cpp-", "sd-server.exe",
			"install-stable-diffusion-server.ps1", stableDiffusionServerStatus,
			chooseSdServerRequested);
		drawPathInput("Model search folder##sd", stableDiffusionModelDirectory,
			"sd-model-folder", chooseSdModelDirectoryRequested);
		ImGui::SameLine(); rescanSdModelsRequested = ImGui::Button("Rescan models##sd");
		drawPathInput("Diffusion model", stableDiffusionModelPath, "sd-model",
			chooseSdModelRequested);
		if (stableDiffusionProcess.running()) {
			ImGui::TextDisabled("Loaded by running server: %s",
				stableDiffusionActiveModelPath.empty() ? "externally managed / unknown"
					: ofFilePath::getFileName(stableDiffusionActiveModelPath).c_str());
			if (!stableDiffusionActiveModelPath.empty() &&
				stableDiffusionActiveModelPath != stableDiffusionModelPath.data())
				ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.22f, 1.0f),
					"Selection changed; the next generation restarts sd-server.");
		}
		const std::string addonsSdTurbo = bundledAddonSdTurboCheckpoint();
		if (!addonsSdTurbo.empty() && stableDiffusionModelPath.data() != addonsSdTurbo) {
			if (ImGui::Button("Use Addons SD-Turbo")) {
				setTextBuffer(stableDiffusionModelDirectory,
					ofFilePath::getEnclosingDirectory(addonsSdTurbo, false));
				setTextBuffer(stableDiffusionModelPath, addonsSdTurbo);
				stableDiffusionCompleteCheckpoint = true;
				applyInferredMediaKind(addonsSdTurbo);
				scanStableDiffusionModels(stableDiffusionModelDirectory.data(),
					detectedDiffusionModels, detectedVaeModels, detectedTextEncoders);
				stableDiffusionServerStatus = "Working Addons SD-Turbo preset selected.";
			}
		}
		ImGui::Checkbox("Complete checkpoint (contains VAE / encoders)", &stableDiffusionCompleteCheckpoint);
		if (!detectedDiffusionModels.empty() && ImGui::BeginCombo("Detected diffusion models", ofFilePath::getFileName(stableDiffusionModelPath.data()).c_str())) {
			for (const auto & path : detectedDiffusionModels) if (ImGui::Selectable(ofFilePath::getFileName(path).c_str(), path == stableDiffusionModelPath.data())) {
				setTextBuffer(stableDiffusionModelPath, path);
				stableDiffusionCompleteCheckpoint = ofToLower(ofFilePath::getFileExt(path)) != "gguf";
				applyInferredMediaKind(path);
				const std::string name = ofToLower(ofFilePath::getFileName(path));
				if (name.find("wan") != std::string::npos) {
					setTextBuffer(stableDiffusionVaePath, preferredModelComponent(detectedVaeModels,
						{ name.find("2.2") != std::string::npos ? "wan2.2" : "wan2.1", "wan" }));
					setTextBuffer(stableDiffusionTextEncoderPath,
						preferredModelComponent(detectedTextEncoders, { "umt5", "t5" }));
				}
			}
			ImGui::EndCombo();
		}
		ImGui::BeginDisabled(stableDiffusionCompleteCheckpoint);
		drawPathInput("VAE (optional)", stableDiffusionVaePath, "sd-vae",
			chooseSdVaeRequested);
		if (!detectedVaeModels.empty() && ImGui::BeginCombo("Detected VAEs", ofFilePath::getFileName(stableDiffusionVaePath.data()).c_str())) {
			for (const auto & path : detectedVaeModels) if (ImGui::Selectable(ofFilePath::getFileName(path).c_str(), path == stableDiffusionVaePath.data())) setTextBuffer(stableDiffusionVaePath, path);
			ImGui::EndCombo();
		}
		drawPathInput("CLIP-L (SD3/Flux)", stableDiffusionClipLPath, "sd-clip-l",
			chooseSdClipLRequested);
		drawPathInput("CLIP-G (SD3)", stableDiffusionClipGPath, "sd-clip-g",
			chooseSdClipGRequested);
		drawPathInput("T5XXL (SD3/Flux)", stableDiffusionTextEncoderPath, "sd-t5",
			chooseSdTextEncoderRequested);
		if (!detectedTextEncoders.empty() && ImGui::BeginCombo("Detected T5/text encoders", ofFilePath::getFileName(stableDiffusionTextEncoderPath.data()).c_str())) {
			for (const auto & path : detectedTextEncoders) if (ImGui::Selectable(ofFilePath::getFileName(path).c_str(), path == stableDiffusionTextEncoderPath.data())) setTextBuffer(stableDiffusionTextEncoderPath, path);
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
		if (stableDiffusionCompleteCheckpoint)
			ImGui::TextDisabled("VAE and text encoders are loaded from the complete checkpoint.");
		ImGui::Checkbox("Flash Attention##sd", &stableDiffusionFlashAttention);
		ImGui::SameLine(); ImGui::Checkbox("Offload to CPU##sd", &stableDiffusionOffloadToCpu);
		}
	}
	ImGui::TextDisabled("Capabilities: Image: %s | Video: %s",
		mediaProfile.supportsImage ? "yes" : "no",
		selectedMediaBackend == 2 ? "checked from loaded model" :
			(mediaProfile.supportsVideo ? "yes" : "no"));
	ImGui::TextWrapped("%s", mediaProfile.capabilityNote);
	const bool inspectedContextMatchesSelection = selectedMediaBackend == 2 &&
		currentMediaCapabilities &&
		ofxICExample::mediaModelMatches(currentMediaCapabilities.model,
			stableDiffusionModelPath.data());
	if (selectedMediaBackend == 2 && currentMediaCapabilities) {
		ImGui::SeparatorText("Loaded context");
		ImGui::Text("Model: %s", currentMediaCapabilities.model.empty()
			? "<unknown>" : currentMediaCapabilities.model.c_str());
		ImGui::Text("Mode: %s", currentMediaCapabilities.currentMode.empty()
			? "<unspecified>" : currentMediaCapabilities.currentMode.c_str());
		ImGui::TextDisabled("Image: %s | Video: %s%s",
			currentMediaCapabilities.supports(ofxIC::MediaKind::Image) ? "yes" : "no",
			currentMediaCapabilities.supports(ofxIC::MediaKind::Video) ? "yes" : "no",
			inspectedContextMatchesSelection ? "" : " | selection differs");
		if (currentMediaCapabilities.minWidth > 0 && currentMediaCapabilities.maxWidth > 0 &&
			currentMediaCapabilities.minHeight > 0 && currentMediaCapabilities.maxHeight > 0)
			ImGui::TextDisabled("Size limits: %d-%d x %d-%d",
				currentMediaCapabilities.minWidth, currentMediaCapabilities.maxWidth,
				currentMediaCapabilities.minHeight, currentMediaCapabilities.maxHeight);
		if (currentMediaCapabilities.defaultWidth > 0 && currentMediaCapabilities.defaultHeight > 0)
			ImGui::TextDisabled("Server defaults: %dx%d | %d frames | %d FPS | %s",
				currentMediaCapabilities.defaultWidth, currentMediaCapabilities.defaultHeight,
				currentMediaCapabilities.defaultVideoFrames, currentMediaCapabilities.defaultFps,
				currentMediaCapabilities.defaultOutputFormat.empty() ? "format unspecified" :
					currentMediaCapabilities.defaultOutputFormat.c_str());
		if (inspectedContextMatchesSelection)
			applyMediaDefaultsRequested = ImGui::Button("Apply server defaults");
	}
	if (selectedMediaBackend == 2) {
		const auto inferred = ofxICExample::inferMediaModelKind(stableDiffusionModelPath.data());
		if (inferred)
			ImGui::TextDisabled("Selected model type: %s (inferred from model family)",
				*inferred == ofxICExample::MediaModelKind::Video ? "Video" : "Image");
	}
	const bool effectiveImageSupport = mediaProfile.supportsImage &&
		(!inspectedContextMatchesSelection ||
			currentMediaCapabilities.supports(ofxIC::MediaKind::Image));
	const bool effectiveVideoSupport = mediaProfile.supportsVideo &&
		(!inspectedContextMatchesSelection ||
			currentMediaCapabilities.supports(ofxIC::MediaKind::Video));
	if (effectiveImageSupport && effectiveVideoSupport) {
		ImGui::Combo("Kind", &selectedMediaKind, mediaKinds, 2);
	} else {
		selectedMediaKind = effectiveVideoSupport ? 1 : 0;
		ImGui::Text("Kind: %s", mediaKinds[selectedMediaKind]);
	}
	if (selectedMediaBackend != 1) {
		drawEndpointInput("Media base URL", mediaEndpointUrl, "media",
			&mediaConfigurationDirty);
	} else {
		ImGui::TextDisabled("fal-ai through Hugging Face routing");
	}
	if (selectedMediaBackend != 2) {
		auto & mediaModel = selectedMediaKind == 0 ? mediaImageModel : mediaVideoModel;
		ImGui::InputText("Media model", mediaModel.data(), mediaModel.size());
	}
	ImGui::InputTextMultiline(
		"##media-prompt",
		mediaInput.data(),
		mediaInput.size(),
		ImVec2(-1, 64));
	ImGui::InputInt("Width", &mediaWidth);
	ImGui::SameLine();
	ImGui::InputInt("Height", &mediaHeight);
	if (selectedMediaKind == 1) {
		ImGui::InputInt("Frames", &mediaFrames);
		ImGui::SameLine();
		ImGui::InputInt("FPS", &mediaFps);
	}
	if (selectedMediaBackend == 2 && ImGui::CollapsingHeader("Advanced generation")) {
		ImGui::InputInt("Steps", &mediaSteps);
		ImGui::SliderFloat("Guidance", &mediaGuidance, 0.0f, 30.0f, "%.2f");
		ImGui::InputInt("Seed (-1 = random)", &mediaSeed);
		const auto capabilityChoice = [](const char * label, std::string & selected,
			const std::vector<std::string> & values) {
			const char * preview = selected.empty() ? "server default" : selected.c_str();
			if (!ImGui::BeginCombo(label, preview)) return;
			if (ImGui::Selectable("server default", selected.empty())) selected.clear();
			for (const auto & value : values)
				if (ImGui::Selectable(value.c_str(), selected == value)) selected = value;
			ImGui::EndCombo();
		};
		capabilityChoice("Sampler", mediaSampler, currentMediaCapabilities.samplers);
		capabilityChoice("Scheduler", mediaScheduler, currentMediaCapabilities.schedulers);
		const auto & formats = selectedMediaKind == 1
			? currentMediaCapabilities.videoOutputFormats
			: currentMediaCapabilities.imageOutputFormats;
		capabilityChoice("Output format", mediaOutputFormat, formats);
		if (!currentMediaCapabilities)
			ImGui::TextDisabled("Inspect the loaded context to populate capability choices.");
	}
	const char * generateLabel = selectedMediaBackend == 1
		? (selectedMediaKind == 0 ? "Generate HF image" : "Submit HF video")
		: (selectedMediaBackend == 2
			? (selectedMediaKind == 0 ? "Generate local image" : "Generate local video")
			: "Generate OpenAI image");
	generateMediaRequested = ImGui::Button(generateLabel);
	if (selectedMediaBackend == 2) {
		ImGui::SameLine();
		ImGui::BeginDisabled(stableDiffusionProcess.state() !=
			ofxICExample::ManagedProcessState::Ready);
		inspectMediaContextRequested = ImGui::Button("Inspect loaded context");
		ImGui::EndDisabled();
	}
	ImGui::EndDisabled();
	if (mediaBusy) {
		ImGui::SameLine();
		cancelRequested = ImGui::Button("Cancel media request") || cancelRequested;
	}
	const std::string mediaToken = configuredMediaToken();
	drawAuthentication("media", mediaToken, configuredMediaTokenSource(),
		mediaBackends[selectedMediaBackend].tokenEnvironment, mediaTokenInput,
		saveMediaTokenRequested, forgetMediaTokenRequested);
	if (mediaBusy) ImGui::TextDisabled("Waiting for media endpoint...");
	ImGui::TextWrapped("%s", mediaStatus.c_str());
	if (!mediaOutput.empty()) ImGui::TextWrapped("%s", mediaOutput.c_str());
	if (generatedImage.isAllocated()) {
		ImGui::Image(
			(ImTextureID)(uintptr_t)generatedImage.getTexture().getTextureData().textureID,
			fitMediaPreview(generatedImage.getWidth(), generatedImage.getHeight()));
	} else if (generatedVideo.isLoaded()) {
		ImGui::Image(
			(ImTextureID)(uintptr_t)generatedVideo.getTexture().getTextureData().textureID,
			fitMediaPreview(generatedVideo.getWidth(), generatedVideo.getHeight()));
		ImGui::TextDisabled("Playback: frame %d / %d | %.1f%% | %s",
			generatedVideo.getCurrentFrame(), generatedVideo.getTotalNumFrames(),
			generatedVideo.getPosition() * 100.0f,
			generatedVideo.isPaused() ? "paused" :
				(generatedVideo.isPlaying() ? "playing" : "stopped"));
		if (ImGui::Button(generatedVideo.isPaused() ? "Resume video" : "Pause video"))
			generatedVideo.setPaused(!generatedVideo.isPaused());
		ImGui::SameLine();
		if (ImGui::Button("Restart video")) {
			generatedVideo.setPosition(0.0f);
			generatedVideo.setPaused(false);
			generatedVideo.play();
		}
	}
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("Music")) {
	ImGui::BeginDisabled(taskLocked);
	if (ImGui::BeginCombo("Music backend", musicBackends[selectedMusicBackend].name)) {
		for (std::size_t index = 0; index < musicBackends.size(); ++index) {
			const bool selected = selectedMusicBackend == static_cast<int>(index);
			if (ImGui::Selectable(musicBackends[index].name, selected)) {
				selectMusicBackend(static_cast<int>(index));
			}
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::TextDisabled("%s", musicBackends[selectedMusicBackend].capabilityNote);
	if (selectedMusicBackend == 0) {
		drawRuntimeControls(aceStepProcess, aceStepServerStatus, "acestep",
			startAceStepServerRequested, stopAceStepServerRequested);
		if (ImGui::CollapsingHeader("Runtime settings##acestep")) {
		drawPathInput("ACE-Step server", aceStepServerPath, "acestep-server",
			chooseAceStepServerRequested);
		ImGui::SameLine();
		if (ImGui::Button("Detect installed##ace")) {
			const std::string detected = installedAceStepServer();
			detectedAceStepServerPath = detected;
			if (!detected.empty()) {
				setTextBuffer(aceStepServerPath, detected);
				if (managedAceStepArguments(aceStepServerArguments.data()))
					setTextBuffer(aceStepServerArguments, defaultAceStepArguments(
						aceStepServerPath.data(), aceStepModelDirectory.data()));
				aceStepServerStatus = isNativeAceStepServer(detected)
					? "Script-installed native acestep.cpp server selected."
					: "Script-installed ACE-Step 1.5 Python environment selected.";
			} else aceStepServerStatus = "No ACE-Step server found. Run scripts\\install-acestep-server.ps1.";
			ofLogNotice("ofxIC servers") << "Detect ACE-Step: " << (detected.empty() ? "not found" : detected);
		}
		drawPathInput("Model folder##ace", aceStepModelDirectory, "acestep-model-folder",
			chooseAceStepModelDirectoryRequested);
		if (isNativeAceStepServer(aceStepServerPath.data())) {
			ImGui::TextDisabled("GGUF set: %s", hasNativeAceStepModels(aceStepModelDirectory.data())
				? "LM + embedding + DiT + VAE ready" : "incomplete or not found");
		}
		ImGui::InputText("Server arguments", aceStepServerArguments.data(), aceStepServerArguments.size());
		}
	}
	drawEndpointInput("Music base URL", musicEndpointUrl, "music",
		&musicConfigurationDirty);
	ImGui::InputTextMultiline(
		"##music-prompt", musicInput.data(), musicInput.size(), ImVec2(-1, 58));
	ImGui::InputInt("Duration (seconds)", &musicDuration);
	ImGui::SameLine();
	const char * musicFormats[] = { "MP3", "WAV" };
	ImGui::Combo("Format", &musicOutputFormat, musicFormats, 2);
	generateMusicRequested = ImGui::Button("Generate music");
	ImGui::EndDisabled();
	if (mediaBusy) {
		ImGui::SameLine();
		cancelRequested = ImGui::Button("Cancel music request") || cancelRequested;
	}
	if (selectedMusicBackend == 1) {
		const std::string musicToken = configuredMusicToken();
		drawAuthentication("music", musicToken, configuredMusicTokenSource(),
			"STABILITY_API_KEY", musicTokenInput,
			saveMusicTokenRequested, forgetMusicTokenRequested);
	} else {
		ImGui::TextDisabled("Local ACE-Step does not require or receive an API token.");
	}
	ImGui::TextWrapped("%s", musicStatus.c_str());
	if (!musicOutput.empty()) ImGui::TextWrapped("%s", musicOutput.c_str());
	if (generatedMusic.isLoaded()) {
		if (ImGui::Button("Play generated music")) generatedMusic.play();
		ImGui::SameLine();
		if (ImGui::Button("Stop generated music")) generatedMusic.stop();
	}
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("SAM")) {
	ImGui::TextUnformatted("SAM bridge v1");
	ImGui::TextDisabled("External endpoint: PPM + normalized points -> PGM mask");
	drawRuntimeControls(samBridgeProcess, samBridgeProcessStatus, "sam",
		startSamBridgeRequested, stopSamBridgeRequested);
	if (ImGui::CollapsingHeader("Runtime settings##sam")) {
		if (ImGui::Button("Detect installed SAM runtime")) {
			const std::string python = installedServerExecutable("sam-python-", "python.exe");
			const std::string runner = installedServerExecutable("sam-python-", "sam-python-runner.py");
			const std::string model = detectedSamModel();
			detectedSamPythonPath = python;
			detectedSamRunnerPath = runner;
			if (!python.empty()) setTextBuffer(samBridgeExecutablePath, python);
			if (!runner.empty()) setTextBuffer(samRunnerPath, runner);
			if (!model.empty()) setTextBuffer(samModelPath, model);
			samBridgeProcessStatus = !python.empty() && !runner.empty()
				? (model.empty() ? "Installed SAM runtime selected; choose a checkpoint."
					: "Installed SAM runtime and checkpoint selected.")
				: "No script-installed SAM runtime found. Run scripts\\install-sam-server.ps1.";
		}
		drawPathInput("Python executable", samBridgeExecutablePath, "sam-python",
			chooseSamBridgeRequested);
		drawPathInput("SAM runner", samRunnerPath, "sam-runner",
			chooseSamRunnerRequested);
		drawPathInput("SAM checkpoint", samModelPath, "sam-checkpoint",
			chooseSamModelRequested);
		ImGui::Checkbox("CUDA 13##sam", &samCuda);
		ImGui::InputText("Extra bridge arguments", samBridgeArguments.data(), samBridgeArguments.size());
		ImGui::TextDisabled("The GUI starts the bundled bridge and the selected external SAM runner on port 18085.");
	}
	ImGui::BeginDisabled(taskLocked);
	drawEndpointInput("SAM base URL", segmentationEndpointUrl, "sam");
	inspectSegmentationBridgeRequested = ImGui::Button("Check bridge");
	ImGui::SameLine();
	loadSegmentationImageRequested = ImGui::Button("Load segmentation image");
	ImGui::SameLine();
	ImGui::BeginDisabled(segmentationImageBytes.empty());
	segmentImageRequested = ImGui::Button("Segment prompts");
	ImGui::EndDisabled();
	ImGui::SliderFloat("Point X", &segmentationPointX, 0.0f, 1.0f);
	ImGui::SliderFloat("Point Y", &segmentationPointY, 0.0f, 1.0f);
	if (ImGui::Button("Add positive")) {
		segmentationPoints.push_back({ segmentationPointX, segmentationPointY, true });
	}
	ImGui::SameLine();
	if (ImGui::Button("Add negative")) {
		segmentationPoints.push_back({ segmentationPointX, segmentationPointY, false });
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(segmentationPoints.empty());
	if (ImGui::Button("Undo prompt")) segmentationPoints.pop_back();
	ImGui::SameLine();
	if (ImGui::Button("Clear prompts")) segmentationPoints.clear();
	ImGui::EndDisabled();
	ImGui::Text("Queued prompts: %d", static_cast<int>(segmentationPoints.size()));
	for (size_t i = 0; i < segmentationPoints.size(); ++i) {
		const auto & point = segmentationPoints[i];
		ImGui::BulletText("%s  x %.3f  y %.3f", point.positive ? "+" : "-", point.x, point.y);
	}
	if (segmentationImage.isAllocated()) {
		ImGui::TextDisabled("Input: left click positive, right click negative");
		const ImVec2 previewSize = fitMediaPreview(
			segmentationImage.getWidth(), segmentationImage.getHeight());
		ImGui::Image(
			(ImTextureID)(uintptr_t)segmentationImage.getTexture().getTextureData().textureID,
			previewSize);
		const ImVec2 imageMin = ImGui::GetItemRectMin();
		const bool hovered = ImGui::IsItemHovered();
		if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
			ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
			const ImVec2 mouse = ImGui::GetMousePos();
			segmentationPointX = ofClamp((mouse.x - imageMin.x) / previewSize.x, 0.0f, 1.0f);
			segmentationPointY = ofClamp((mouse.y - imageMin.y) / previewSize.y, 0.0f, 1.0f);
			segmentationPoints.push_back({ segmentationPointX, segmentationPointY,
				ImGui::IsMouseClicked(ImGuiMouseButton_Left) });
		}
		ImDrawList * drawList = ImGui::GetWindowDrawList();
		for (const auto & point : segmentationPoints) {
			const ImVec2 position(
				imageMin.x + point.x * previewSize.x,
				imageMin.y + point.y * previewSize.y);
			const ImU32 color = point.positive
				? IM_COL32(40, 220, 90, 255)
				: IM_COL32(240, 70, 70, 255);
			drawList->AddCircleFilled(position, 5.0f, color);
			drawList->AddCircle(position, 7.0f, IM_COL32(255, 255, 255, 230), 16, 2.0f);
		}
	}
	ImGui::EndDisabled();
	ImGui::TextWrapped("%s", segmentationStatus.c_str());
	if (segmentationMaskImage.isAllocated()) {
		ImGui::Image(
			(ImTextureID)(uintptr_t)segmentationMaskImage.getTexture().getTextureData().textureID,
			fitMediaPreview(segmentationMaskImage.getWidth(), segmentationMaskImage.getHeight()));
	}
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("History")) {
		ImGui::SeparatorText("Recent tasks");
		ImGui::TextWrapped(
			"Prompts, document contents, generated payloads, and credentials are not stored. "
			"History keeps only task type, outcome, status, timestamp, and a saved output path.");
		ImGui::TextDisabled("%s", jobHistory.status().c_str());
		ImGui::SameLine();
		ImGui::BeginDisabled(jobHistory.entries().empty());
		clearHistoryRequested = ImGui::Button("Clear history");
		ImGui::EndDisabled();
		if (ImGui::BeginTable("task-history", 5,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
			ImVec2(0, 360))) {
			ImGui::TableSetupColumn("Time");
			ImGui::TableSetupColumn("Task");
			ImGui::TableSetupColumn("Outcome");
			ImGui::TableSetupColumn("Status");
			ImGui::TableSetupColumn("Output path");
			ImGui::TableHeadersRow();
			for (auto entry = jobHistory.entries().rbegin();
				entry != jobHistory.entries().rend(); ++entry) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(entry->timestamp.c_str());
				ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(entry->task.c_str());
				ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(entry->outcome.c_str());
				ImGui::TableSetColumnIndex(3); ImGui::TextWrapped("%s", entry->detail.c_str());
				ImGui::TableSetColumnIndex(4);
				ImGui::TextWrapped("%s", entry->outputPath.empty() ? "-" : entry->outputPath.c_str());
			}
			ImGui::EndTable();
		}
		ImGui::EndTabItem();
	}
	ImGui::EndTabBar();
	}
	ImGui::End();
	gui.end();
	if (rescanInstalledRuntimesRequested) {
		rescanInstalledRuntimes();
		runtimeSetupStatus = "Rescan completed. Detection refreshed; selected paths, models and running servers unchanged. "
			"Use Detect installed in a task tab to select a detected executable.";
		ofLogNotice("ofxIC servers") << runtimeSetupStatus;
	}
	if (exportDiagnosticsRequested) {
		const std::string filename = "ofxIC-diagnostics-" +
			ofGetTimestampString("%Y%m%d-%H%M%S") + ".txt";
		ofFileDialogResult selection = ofSystemSaveDialog(
			filename, "Export privacy-aware diagnostics");
		if (selection.bSuccess) exportDiagnostics(selection.getPath());
		else diagnosticsStatus = "Diagnostic export cancelled.";
	}
	if (cancelDeferredRequested) cancelDeferredTask();
	if (clearHistoryRequested && !jobHistory.clear(historyPath))
		ofLogError("ofxIC history") << jobHistory.status();
	if (loadLlamaModelRequested) {
		ofFileDialogResult selection = ofSystemLoadDialog("Choose a GGUF model");
		if (selection.bSuccess && ofToLower(ofFilePath::getFileExt(selection.getPath())) == "gguf") {
			setTextBuffer(llamaModelPath, selection.getPath());
			llamaServerStatus = "Model selected; ready to start.";
		} else if (selection.bSuccess) {
			llamaServerStatus = "Rejected model: choose a .gguf file.";
		}
	}
	if (chooseLlamaModelDirectoryRequested) {
		ofFileDialogResult selection = ofSystemLoadDialog(
			"Choose a folder containing GGUF models", true, llamaModelDirectory.data());
		if (selection.bSuccess) {
			setTextBuffer(llamaModelDirectory, selection.getPath());
			detectedLlamaModels = detectedLlamaModelPaths(llamaModelDirectory.data());
			llamaServerStatus = "Model folder selected; found " +
				ofToString(detectedLlamaModels.size()) + " GGUF file(s).";
		}
	}
	if (rescanLlamaModelsRequested) {
		detectedLlamaModels = detectedLlamaModelPaths(llamaModelDirectory.data());
		llamaServerStatus = "Model folders rescanned; found " +
			ofToString(detectedLlamaModels.size()) + " GGUF file(s).";
	}
	if (startLlamaServerRequested) startLocalLlamaServer();
	if (stopLlamaServerRequested) stopLocalLlamaServer();
	auto choosePath = [](const char * title, auto & destination) {
		ofFileDialogResult selection = ofSystemLoadDialog(title);
		if (selection.bSuccess) setTextBuffer(destination, selection.getPath());
	};
	if (chooseLlamaServerRequested) choosePath("Choose llama-server.exe", llamaServerPath);
	if (chooseSdServerRequested) choosePath("Choose sd-server.exe", stableDiffusionServerPath);
	if (chooseSdModelDirectoryRequested) {
		ofFileDialogResult selection = ofSystemLoadDialog("Choose Stable Diffusion model folder", true, stableDiffusionModelDirectory.data());
		if (selection.bSuccess) setTextBuffer(stableDiffusionModelDirectory, selection.getPath());
		rescanSdModelsRequested = selection.bSuccess;
	}
	if (rescanSdModelsRequested) {
		scanStableDiffusionModels(stableDiffusionModelDirectory.data(), detectedDiffusionModels,
			detectedVaeModels, detectedTextEncoders);
		stableDiffusionServerStatus = "Found " + ofToString(detectedDiffusionModels.size()) +
			" diffusion, " + ofToString(detectedVaeModels.size()) + " VAE, and " +
			ofToString(detectedTextEncoders.size()) + " text encoder model(s).";
	}
	if (chooseSdModelRequested) {
		ofFileDialogResult selection = ofSystemLoadDialog("Choose diffusion model");
		if (selection.bSuccess) {
			setTextBuffer(stableDiffusionModelPath, selection.getPath());
			stableDiffusionCompleteCheckpoint =
				ofToLower(ofFilePath::getFileExt(selection.getPath())) != "gguf";
			applyInferredMediaKind(selection.getPath());
			const std::string name = ofToLower(ofFilePath::getFileName(selection.getPath()));
			if (name.find("wan") != std::string::npos) {
				setTextBuffer(stableDiffusionVaePath, preferredModelComponent(detectedVaeModels,
					{ name.find("2.2") != std::string::npos ? "wan2.2" : "wan2.1", "wan" }));
				setTextBuffer(stableDiffusionTextEncoderPath,
					preferredModelComponent(detectedTextEncoders, { "umt5", "t5" }));
			}
		}
	}
	if (chooseSdVaeRequested) choosePath("Choose VAE", stableDiffusionVaePath);
	if (chooseSdTextEncoderRequested) choosePath("Choose text encoder", stableDiffusionTextEncoderPath);
	if (chooseSdClipLRequested) choosePath("Choose CLIP-L encoder", stableDiffusionClipLPath);
	if (chooseSdClipGRequested) choosePath("Choose CLIP-G encoder", stableDiffusionClipGPath);
	if (startSdServerRequested) startLocalStableDiffusionServer();
	if (stopSdServerRequested) stopLocalStableDiffusionServer();
	if (chooseAceStepServerRequested) choosePath("Choose ACE-Step server executable", aceStepServerPath);
	if (chooseAceStepModelDirectoryRequested) {
		ofFileDialogResult selection = ofSystemLoadDialog(
			"Choose ACE-Step GGUF model folder", true, aceStepModelDirectory.data());
		if (selection.bSuccess) {
			setTextBuffer(aceStepModelDirectory, selection.getPath());
			if (managedAceStepArguments(aceStepServerArguments.data()))
				setTextBuffer(aceStepServerArguments, defaultAceStepArguments(
					aceStepServerPath.data(), aceStepModelDirectory.data()));
			aceStepServerStatus = hasNativeAceStepModels(aceStepModelDirectory.data())
				? "Complete ACE-Step GGUF model set selected."
				: "Selected folder does not contain LM, embedding, DiT and VAE GGUFs.";
		}
	}
	if (startAceStepServerRequested) startLocalAceStepServer();
	if (stopAceStepServerRequested) stopLocalAceStepServer();
	if (chooseWhisperServerRequested) choosePath("Choose whisper-server.exe", whisperServerPath);
	if (chooseWhisperModelRequested) choosePath("Choose whisper model", whisperModelPath);
	if (startWhisperServerRequested) startLocalWhisperServer();
	if (stopWhisperServerRequested) stopLocalWhisperServer();
	if (chooseSamBridgeRequested) choosePath("Choose SAM bridge executable", samBridgeExecutablePath);
	if (chooseSamRunnerRequested) choosePath("Choose SAM runner", samRunnerPath);
	if (chooseSamModelRequested) choosePath("Choose Meta SAM checkpoint", samModelPath);
	if (startSamBridgeRequested) startLocalSamBridge();
	if (stopSamBridgeRequested) stopLocalSamBridge();
	if (loadDocumentRequested && !taskLocked) {
		ofFileDialogResult selection = ofSystemLoadDialog("Load a Markdown or text document");
		if (selection.bSuccess) loadDocument(selection.getPath());
	}
	if (loadAudioRequested && !taskLocked) {
		ofFileDialogResult selection = ofSystemLoadDialog("Load an audio file");
		if (selection.bSuccess) loadAudio(selection.getPath());
	}
	if (loadSegmentationImageRequested && !taskLocked) {
		ofFileDialogResult selection = ofSystemLoadDialog("Load an image for SAM segmentation");
		if (selection.bSuccess) loadSegmentationImage(selection.getPath());
	}
	if (applyRequested) applyConfiguration();
	if (saveSettingsRequested) saveExampleSettings();
	if (resetSettingsRequested) resetExampleSettings();
	if (inspectRequested) {
		if (configurationDirty) applyConfiguration();
		inspectEndpoint();
	}
	if (clearRequested && !busy) {
		chat.clear();
		lastMessage.clear();
		output.clear();
		status = "Conversation cleared";
		focusMessageInput = true;
	}
	if (sendRequested) {
		if (configurationDirty) applyConfiguration();
		sendMessage();
	}
	if (transcribeAudioRequested) {
		if (configurationDirty) applyConfiguration();
		transcribeAudio();
	}
	if (inspectSegmentationBridgeRequested) inspectSegmentationBridge();
	if (segmentImageRequested) {
		if (configurationDirty) applyConfiguration();
		segmentImage();
	}
	if (cancelRequested) cancelRequest();
	if (saveTokenRequested) {
		saveTokenCredential(endpointProfiles[selectedProfile].tokenEnvironment, tokenInput);
	}
	if (forgetTokenRequested) {
		forgetTokenCredential(endpointProfiles[selectedProfile].tokenEnvironment);
	}
	if (saveMediaTokenRequested) {
		saveTokenCredential(mediaBackends[selectedMediaBackend].tokenEnvironment, mediaTokenInput);
	}
	if (forgetMediaTokenRequested) {
		forgetTokenCredential(mediaBackends[selectedMediaBackend].tokenEnvironment);
	}
	if (generateMediaRequested) {
		if (mediaConfigurationDirty) applyMediaConfiguration();
		generateMedia();
	}
	if (inspectMediaContextRequested) {
		if (mediaConfigurationDirty) applyMediaConfiguration();
		inspectMediaContext();
	}
	if (applyMediaDefaultsRequested) {
		ofxICExample::applySafeMediaDefaults(currentMediaCapabilities,
			mediaWidth, mediaHeight, mediaFrames, mediaFps);
	}
	if (saveMusicTokenRequested) {
		saveTokenCredential("STABILITY_API_KEY", musicTokenInput);
	}
	if (forgetMusicTokenRequested) forgetTokenCredential("STABILITY_API_KEY");
	if (generateMusicRequested) {
		if (musicConfigurationDirty) applyMusicConfiguration();
		generateMusic();
	}
}

void ofApp::keyPressed(int key) {
	if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard) return;
	if (key == OF_KEY_F1) {
		if (configurationDirty) applyConfiguration();
		if (!mediaBusy) inspectEndpoint();
		return;
	}
	if (key == OF_KEY_F2) {
		if (!busy) {
			chat.clear();
			lastMessage.clear();
			output.clear();
			status = "Conversation cleared";
			focusMessageInput = true;
		}
		return;
	}
	if (key == OF_KEY_BACKSPACE) {
		if (input[0] && !busy) input[std::strlen(input.data()) - 1] = '\0';
		return;
	}
	if (key == OF_KEY_RETURN) {
		if (configurationDirty) applyConfiguration();
		sendMessage();
		return;
	}
	if (key >= 32 && key <= 126 && !busy) {
		const std::size_t length = std::strlen(input.data());
		if (length + 1 < input.size()) {
			input[length] = static_cast<char>(key);
			input[length + 1] = '\0';
		}
	}
}

void ofApp::dragEvent(ofDragInfo dragInfo) {
	if (busy || mediaBusy) {
		documentStatus = "Wait for the current request before loading documents.";
		return;
	}
	for (const auto & path : dragInfo.files) loadDocument(path.string());
}

void ofApp::exit() {
	cancellationRequested = true;
	finishWorker();
	finishMediaWorker();
	stopLocalLlamaServer();
	stopLocalStableDiffusionServer();
	stopLocalAceStepServer();
	stopLocalWhisperServer();
	stopLocalSamBridge();
}

void ofApp::startLocalLlamaServer() {
	if (llamaProcess.running()) return;
	if (llamaProcess.useExisting("llama-server", 8080)) {
		llamaProcess.followOutputFiles(localServerLogFiles("llama-server", 8080));
		setTextBuffer(endpointUrl, "http://127.0.0.1:8080");
		configurationDirty = true;
		llamaServerStatus = llamaProcess.status();
		return;
	}
	detectedLlamaServerPath = ofxICExample::resolveInstalledExecutable(
		llamaServerPath.data(), detectedLlamaServerPath, installedServerRoot(),
		"llama.cpp-", "llama-server.exe");
	if (detectedLlamaServerPath.empty()) {
		const std::string configured = llamaServerPath.data();
		llamaServerStatus = configured.empty()
			? "llama-server executable not found under " + installedServerRoot() + "."
			: "llama-server executable not found: " + configured;
		ofLogError("ofxIC servers") << llamaServerStatus << "\n" <<
			runtimeExecutableFailureDiagnostic(configured, detectedLlamaServerPath,
				"llama.cpp-", "llama-server.exe");
		return;
	}
	setTextBuffer(llamaServerPath, detectedLlamaServerPath);
	if (!configuredPath(llamaModelPath.data())) {
		llamaServerStatus = "Choose a GGUF model before starting llama-server.";
		return;
	}
	ofLogNotice("ofxIC servers") << "Starting llama-server: " << llamaServerPath.data();
	std::vector<std::string> arguments{
		"--model", llamaModelPath.data(), "--host", "127.0.0.1", "--port", "8080",
		"--ctx-size", ofToString(std::max(512, llamaContextSize)),
		"--n-gpu-layers", ofToString(std::max(0, llamaGpuLayers)) };
	if (llamaFlashAttention) arguments.insert(arguments.end(), { "--flash-attn", "on" });
	if (llamaProcess.start(llamaServerPath.data(), arguments, "llama-server", 8080)) {
		setTextBuffer(endpointUrl, "http://127.0.0.1:8080");
		configurationDirty = true;
	}
	llamaServerStatus = llamaProcess.status();
}

void ofApp::stopLocalLlamaServer() {
	llamaProcess.stop();
	llamaServerStatus = llamaProcess.status();
}

void ofApp::updateLocalLlamaServer() {
	updateManagedProcess(llamaProcess, llamaServerStatus, "llama-server");
}

void ofApp::updateManagedProcess(ofxICExample::ManagedProcess & process,
	std::string & processStatus, const std::string & logName) {
	const bool observable = process.running() ||
		process.state() == ofxICExample::ManagedProcessState::Exited ||
		process.state() == ofxICExample::ManagedProcessState::Failed;
	if (!observable) return;
	process.update();
	processStatus = process.status();
	const std::string output = process.takeNewOutput();
	if (!output.empty()) ofLogNotice("ofxIC servers") << logName << ":\n" << output;
}

void ofApp::configureRuntimeAutomation() {
	runtimeAutomation = ofToLower(environmentValue("OFXIC_RUNTIME_AUTOSTART"));
	if (runtimeAutomation.empty()) return;
	runtimeAutomationResultPath = environmentValue("OFXIC_RUNTIME_RESULT_PATH");
	if (runtimeAutomationResultPath.empty()) {
		ofLogError("ofxIC runtime matrix") <<
			"OFXIC_RUNTIME_AUTOSTART requires OFXIC_RUNTIME_RESULT_PATH.";
		runtimeAutomation.clear();
		return;
	}
	if (runtimeAutomation == "sd") runtimeAutomation = "stable-diffusion";
	if (runtimeAutomation == "acestep") runtimeAutomation = "ace-step";
	if (runtimeAutomation != "llama" && runtimeAutomation != "stable-diffusion" &&
		runtimeAutomation != "ace-step" && runtimeAutomation != "whisper" &&
		runtimeAutomation != "sam") {
		finishRuntimeAutomation("failed", "Unknown runtime name: " + runtimeAutomation);
		return;
	}
	const int configuredTimeout = ofToInt(environmentValue("OFXIC_RUNTIME_TIMEOUT_SECONDS"));
	if (configuredTimeout > 0) {
		runtimeAutomationTimeoutMillis = static_cast<std::uint64_t>(
			std::clamp(configuredTimeout, 1, 3600)) * 1000ULL;
	}
	runtimeAutomationPlanOnly = environmentValue("OFXIC_RUNTIME_PLAN") == "1";
	runtimeAutomationStartedAtMillis = ofGetElapsedTimeMillis();
	if (runtimeAutomationPlanOnly) return;

	ofxICExample::ManagedProcess * process = nullptr;
	if (runtimeAutomation == "llama") {
		startLocalLlamaServer();
		process = &llamaProcess;
	} else if (runtimeAutomation == "stable-diffusion") {
		startLocalStableDiffusionServer();
		process = &stableDiffusionProcess;
	} else if (runtimeAutomation == "ace-step") {
		startLocalAceStepServer();
		process = &aceStepProcess;
	} else if (runtimeAutomation == "whisper") {
		startLocalWhisperServer();
		process = &whisperProcess;
	} else if (runtimeAutomation == "sam") {
		startLocalSamBridge();
		process = &samBridgeProcess;
	}
	ofLogNotice("ofxIC runtime matrix") << "GUI start path selected for " <<
		runtimeAutomation << "; state=" <<
		ofxICExample::managedProcessStateLabel(process->state());
}

void ofApp::updateRuntimeAutomation() {
	if (runtimeAutomation.empty() || runtimeAutomationFinished) return;
	if (runtimeAutomationPlanOnly) {
		finishRuntimeAutomation("planned", "Configuration inspected; no process was started.");
		return;
	}

	ofxICExample::ManagedProcess * process = nullptr;
	std::string * processStatus = nullptr;
	if (runtimeAutomation == "llama") {
		process = &llamaProcess;
		processStatus = &llamaServerStatus;
	} else if (runtimeAutomation == "stable-diffusion") {
		process = &stableDiffusionProcess;
		processStatus = &stableDiffusionServerStatus;
	} else if (runtimeAutomation == "ace-step") {
		process = &aceStepProcess;
		processStatus = &aceStepServerStatus;
	} else if (runtimeAutomation == "whisper") {
		process = &whisperProcess;
		processStatus = &whisperServerStatus;
	} else if (runtimeAutomation == "sam") {
		process = &samBridgeProcess;
		processStatus = &samBridgeProcessStatus;
	}
	if (!process || !processStatus) {
		finishRuntimeAutomation("failed", "Runtime automation has no process mapping.");
		return;
	}

	using State = ofxICExample::ManagedProcessState;
	if (process->state() == State::Ready) {
		finishRuntimeAutomation("ready", *processStatus);
	} else if (process->state() == State::Failed || process->state() == State::Exited ||
		process->state() == State::Stopped) {
		finishRuntimeAutomation("failed", *processStatus);
	} else if (ofGetElapsedTimeMillis() - runtimeAutomationStartedAtMillis >=
		runtimeAutomationTimeoutMillis) {
		finishRuntimeAutomation("timeout", *processStatus);
	}
}

void ofApp::finishRuntimeAutomation(const std::string & state,
	const std::string & statusText) {
	if (runtimeAutomationFinished || runtimeAutomationResultPath.empty()) return;
	runtimeAutomationFinished = true;

	const ofxICExample::ManagedProcess * process = nullptr;
	std::string executable;
	std::string model;
	std::string auxiliary;
	bool configurationReady = false;
	if (runtimeAutomation == "llama") {
		process = &llamaProcess;
		executable = llamaServerPath.data();
		model = llamaModelPath.data();
		configurationReady = configuredPath(executable.c_str()) &&
			configuredPath(model.c_str());
	} else if (runtimeAutomation == "stable-diffusion") {
		process = &stableDiffusionProcess;
		executable = stableDiffusionServerPath.data();
		model = stableDiffusionModelPath.data();
		configurationReady = configuredPath(executable.c_str()) &&
			configuredPath(model.c_str());
	} else if (runtimeAutomation == "ace-step") {
		process = &aceStepProcess;
		executable = aceStepServerPath.data();
		model = aceStepModelDirectory.data();
		configurationReady = configuredPath(executable.c_str()) &&
			(!isNativeAceStepServer(executable) || configuredPath(model.c_str()));
	} else if (runtimeAutomation == "whisper") {
		process = &whisperProcess;
		executable = whisperServerPath.data();
		model = whisperModelPath.data();
		configurationReady = configuredPath(executable.c_str()) &&
			configuredPath(model.c_str());
	} else if (runtimeAutomation == "sam") {
		process = &samBridgeProcess;
		executable = samBridgeExecutablePath.data();
		model = samModelPath.data();
		auxiliary = samRunnerPath.data();
		configurationReady = configuredPath(executable.c_str()) &&
			configuredPath(model.c_str()) && configuredPath(auxiliary.c_str());
	}

	const auto oneLine = [](std::string value) {
		std::replace(value.begin(), value.end(), '\r', ' ');
		std::replace(value.begin(), value.end(), '\n', ' ');
		return value;
	};
	std::ostringstream result;
	result << "runtime=" << runtimeAutomation << '\n'
		<< "state=" << state << '\n'
		<< "process_state=" << (process
			? ofxICExample::managedProcessStateLabel(process->state()) : "unmapped") << '\n'
		<< "ownership=" << (process && process->ownsProcess() ? "owned" :
			(process && process->state() == ofxICExample::ManagedProcessState::Ready
				? "external" : "none")) << '\n'
		<< "pid=" << (process ? process->processId() : 0) << '\n'
		<< "configuration=" << (configurationReady ? "ready" : "incomplete") << '\n'
		<< "executable=" << executable << '\n'
		<< "model=" << model << '\n';
	if (!auxiliary.empty()) result << "auxiliary=" << auxiliary << '\n';
	result << "status=" << oneLine(statusText) << '\n';
	std::string error;
	if (!ofxICExample::writeTextFileAtomically(
		runtimeAutomationResultPath, result.str(), &error)) {
		ofLogError("ofxIC runtime matrix") << "Could not write runtime result: " << error;
	} else {
		ofLogNotice("ofxIC runtime matrix") << runtimeAutomation << " result: " << state;
	}
	ofExit(state == "ready" || state == "planned" ? 0 : 1);
}

const char * ofApp::deferredTaskLabel(DeferredTask task) const {
	switch (task) {
	case DeferredTask::Chat: return "chat";
	case DeferredTask::LlamaInspect: return "endpoint inspection";
	case DeferredTask::Media: return selectedMediaKind == 1 ? "video generation" : "image generation";
	case DeferredTask::Transcription: return "transcription";
	case DeferredTask::Music: return "music generation";
	case DeferredTask::SamInspect: return "SAM inspection";
	case DeferredTask::SamSegment: return "segmentation";
	case DeferredTask::None: return "task";
	}
	return "task";
}

const char * ofApp::deferredTaskHistoryKind(DeferredTask task) const {
	switch (task) {
	case DeferredTask::Chat: return "chat";
	case DeferredTask::LlamaInspect: return "endpoint-inspection";
	case DeferredTask::Media: return selectedMediaKind == 1
		? "video-generation" : "image-generation";
	case DeferredTask::Transcription: return "transcription";
	case DeferredTask::Music: return "music-generation";
	case DeferredTask::SamInspect: return "sam-inspection";
	case DeferredTask::SamSegment: return "segmentation";
	case DeferredTask::None: return "task";
	}
	return "task";
}

void ofApp::setDeferredTaskStatus(const std::string & message) {
	switch (deferredTask) {
	case DeferredTask::Chat:
	case DeferredTask::LlamaInspect: status = message; break;
	case DeferredTask::Media: mediaStatus = message; break;
	case DeferredTask::Transcription: audioStatus = message; status = message; break;
	case DeferredTask::Music: musicStatus = message; break;
	case DeferredTask::SamInspect:
	case DeferredTask::SamSegment: segmentationStatus = message; status = message; break;
	case DeferredTask::None: break;
	}
}

void ofApp::writeDeferredTaskAutomationResult(const std::string & message) {
	switch (deferredTask) {
	case DeferredTask::Media: writeMediaAutomationResult(message, ""); break;
	case DeferredTask::Music: writeMusicAutomationResult(message, ""); break;
	case DeferredTask::Chat:
	case DeferredTask::LlamaInspect:
	case DeferredTask::Transcription:
	case DeferredTask::SamInspect:
	case DeferredTask::SamSegment: writeAutomationResult(message, ""); break;
	case DeferredTask::None: break;
	}
}

bool ofApp::deferUntilRuntimeReady(DeferredTask task) {
	if (deferredTask != DeferredTask::None && deferredTask != task) {
		const DeferredTask queued = deferredTask;
		const DeferredTask requested = task;
		const std::string message = "Another task is already queued: " +
			std::string(deferredTaskLabel(queued)) + ".";
		deferredTask = requested;
		setDeferredTaskStatus(message);
		deferredTask = queued;
		return true;
	}
	ofxICExample::ManagedProcess * process = nullptr;
	std::string * runtimeStatus = nullptr;
	switch (task) {
	case DeferredTask::Chat:
	case DeferredTask::LlamaInspect: process = &llamaProcess; runtimeStatus = &llamaServerStatus; break;
	case DeferredTask::Media: process = &stableDiffusionProcess; runtimeStatus = &stableDiffusionServerStatus; break;
	case DeferredTask::Transcription: process = &whisperProcess; runtimeStatus = &whisperServerStatus; break;
	case DeferredTask::Music: process = &aceStepProcess; runtimeStatus = &aceStepServerStatus; break;
	case DeferredTask::SamInspect:
	case DeferredTask::SamSegment: process = &samBridgeProcess; runtimeStatus = &samBridgeProcessStatus; break;
	case DeferredTask::None: return false;
	}
	if (process->state() == ofxICExample::ManagedProcessState::Ready) return false;
	if (!process->running()) {
		switch (task) {
		case DeferredTask::Chat:
		case DeferredTask::LlamaInspect: startLocalLlamaServer(); break;
		case DeferredTask::Media: startLocalStableDiffusionServer(); break;
		case DeferredTask::Transcription: startLocalWhisperServer(); break;
		case DeferredTask::Music: startLocalAceStepServer(); break;
		case DeferredTask::SamInspect:
		case DeferredTask::SamSegment: startLocalSamBridge(); break;
		case DeferredTask::None: break;
		}
	}
	if (process->state() == ofxICExample::ManagedProcessState::Ready) return false;
	deferredTask = task;
	if (!process->running()) {
		const std::string message = "Could not start runtime for " +
			std::string(deferredTaskLabel(task)) + ": " +
			(runtimeStatus && !runtimeStatus->empty() ? *runtimeStatus : process->status());
		setDeferredTaskStatus(message);
		writeDeferredTaskAutomationResult(message);
		recordTaskHistory(deferredTaskHistoryKind(task), message);
		deferredTask = DeferredTask::None;
		return true;
	}
	const std::string configuredTimeout = environmentValue(
		"OFXIC_RUNTIME_START_TIMEOUT_SECONDS");
	const int requestedTimeout = configuredTimeout.empty() ? 0 : ofToInt(configuredTimeout);
	const int timeoutSeconds = requestedTimeout > 0
		? std::clamp(requestedTimeout, 1, 60 * 60) : 15 * 60;
	deferredTaskDeadlineMillis = ofGetElapsedTimeMillis() +
		static_cast<std::uint64_t>(timeoutSeconds) * 1000ULL;
	setDeferredTaskStatus("Queued " + std::string(deferredTaskLabel(task)) +
		"; the local runtime is starting. It will continue automatically when ready "
		"(startup timeout " + std::to_string(timeoutSeconds) + " s).");
	return true;
}

void ofApp::continueDeferredTask() {
	if (deferredTask == DeferredTask::None) return;
	ofxICExample::ManagedProcess * process = nullptr;
	switch (deferredTask) {
	case DeferredTask::Chat:
	case DeferredTask::LlamaInspect: process = &llamaProcess; break;
	case DeferredTask::Media: process = &stableDiffusionProcess; break;
	case DeferredTask::Transcription: process = &whisperProcess; break;
	case DeferredTask::Music: process = &aceStepProcess; break;
	case DeferredTask::SamInspect:
	case DeferredTask::SamSegment: process = &samBridgeProcess; break;
	case DeferredTask::None: return;
	}
	if (process->state() == ofxICExample::ManagedProcessState::Stopped ||
		process->state() == ofxICExample::ManagedProcessState::Failed ||
		process->state() == ofxICExample::ManagedProcessState::Exited) {
		const std::string message = "Queued " + std::string(deferredTaskLabel(deferredTask)) +
			" failed before execution: " + process->status();
		setDeferredTaskStatus(message);
		writeDeferredTaskAutomationResult(message);
		recordTaskHistory(deferredTaskHistoryKind(deferredTask), message);
		deferredTask = DeferredTask::None;
		return;
	}
	if (ofGetElapsedTimeMillis() >= deferredTaskDeadlineMillis) {
		const std::string message = "Queued " + std::string(deferredTaskLabel(deferredTask)) +
			" timed out while waiting for the local runtime.";
		setDeferredTaskStatus(message);
		writeDeferredTaskAutomationResult(message);
		recordTaskHistory(deferredTaskHistoryKind(deferredTask), message);
		deferredTask = DeferredTask::None;
		return;
	}
	if (process->state() != ofxICExample::ManagedProcessState::Ready) return;
	const DeferredTask readyTask = deferredTask;
	deferredTask = DeferredTask::None;
	deferredTaskDeadlineMillis = 0;
	switch (readyTask) {
	case DeferredTask::Chat: applyConfiguration(); sendMessage(); break;
	case DeferredTask::LlamaInspect: applyConfiguration(); inspectEndpoint(); break;
	case DeferredTask::Media: applyMediaConfiguration(); generateMedia(); break;
	case DeferredTask::Transcription: transcribeAudio(); break;
	case DeferredTask::Music: applyMusicConfiguration(); generateMusic(); break;
	case DeferredTask::SamInspect: inspectSegmentationBridge(); break;
	case DeferredTask::SamSegment: segmentImage(); break;
	case DeferredTask::None: break;
	}
}

void ofApp::cancelDeferredTask() {
	if (deferredTask == DeferredTask::None) return;
	const std::string message = "Queued " + std::string(deferredTaskLabel(deferredTask)) +
		" cancelled. The runtime was left running.";
	setDeferredTaskStatus(message);
	writeDeferredTaskAutomationResult(message);
	recordTaskHistory(deferredTaskHistoryKind(deferredTask), message);
	deferredTask = DeferredTask::None;
	deferredTaskDeadlineMillis = 0;
}

void ofApp::recordTaskHistory(const std::string & task, const std::string & detail,
	const std::string & outputPath) {
	std::string safeDetail = detail.substr(0, 1024);
	std::replace(safeDetail.begin(), safeDetail.end(), '\r', ' ');
	std::replace(safeDetail.begin(), safeDetail.end(), '\n', ' ');
	std::string lower = ofToLower(safeDetail);
	std::string outcome = "finished";
	if (lower.find("cancel") != std::string::npos) outcome = "cancelled";
	else if (lower.find("fail") != std::string::npos ||
		lower.find("error") != std::string::npos ||
		lower.find("timed out") != std::string::npos ||
		lower.find("unavailable") != std::string::npos) outcome = "failed";
	else if (lower.find("complete") != std::string::npos ||
		lower.find("ready") != std::string::npos ||
		lower.find("reachable") != std::string::npos) outcome = "completed";
	jobHistory.add({ {}, task, outcome, safeDetail, outputPath });
	if (!jobHistory.save(historyPath))
		ofLogError("ofxIC history") << "Could not save task history to " << historyPath;
}

std::string ofApp::diagnosticsReport() const {
	const auto fileState = [](const char * path) {
		if (!path || !*path) return std::string("not-configured");
		std::error_code error;
		return std::filesystem::is_regular_file(std::filesystem::path(path), error)
			? std::string("visible") : std::string("configured-unverified");
	};
	std::ostringstream report;
	report << "ofxIC privacy-aware diagnostics\n"
		<< "ofxIC.version=" << ofxIC::versionString << '\n'
		<< "generated=" << ofGetTimestampString("%Y-%m-%d %H:%M:%S") << '\n'
#if defined(_WIN32)
		<< "platform=Windows\n"
#elif defined(__linux__)
		<< "platform=Linux\n"
#elif defined(__APPLE__)
		<< "platform=macOS\n"
#else
		<< "platform=unknown\n"
#endif
		<< "cplusplus=" << __cplusplus << '\n'
		<< "privacy=prompts,credentials,document-content,payloads,and-server-output-omitted\n"
		<< "chat.profile=" << endpointProfiles[selectedProfile].name << '\n'
		<< "chat.endpoint=" << diagnosticEndpoint(endpointUrl.data()) << '\n'
		<< "chat.model=" << diagnosticText(modelId[0] ? modelId.data() : "server-selected") << '\n'
		<< "chat.credential_loaded=" << (configuredToken().empty() ? "no" : "yes") << '\n'
		<< "transcription.protocol=" << (transcriptionProtocol == 1 ? "whisper.cpp" : "openai") << '\n'
		<< "transcription.endpoint=" << diagnosticEndpoint(transcriptionEndpointUrl.data()) << '\n'
		<< "media.backend=" << mediaBackends[selectedMediaBackend].name << '\n'
		<< "media.endpoint=" << diagnosticEndpoint(mediaEndpointUrl.data()) << '\n'
		<< "media.kind=" << (selectedMediaKind == 1 ? "video" : "image") << '\n'
		<< "media.size=" << mediaWidth << 'x' << mediaHeight << '\n'
		<< "media.frames=" << mediaFrames << '\n'
		<< "media.fps=" << mediaFps << '\n'
		<< "media.steps=" << mediaSteps << '\n'
		<< "media.guidance=" << mediaGuidance << '\n'
		<< "media.sampler=" << diagnosticText(mediaSampler.empty() ? "server-default" : mediaSampler) << '\n'
		<< "media.scheduler=" << diagnosticText(mediaScheduler.empty() ? "server-default" : mediaScheduler) << '\n'
		<< "media.output_format=" << diagnosticText(mediaOutputFormat.empty() ? "server-default" : mediaOutputFormat) << '\n'
		<< "music.backend=" << musicBackends[selectedMusicBackend].name << '\n'
		<< "music.endpoint=" << diagnosticEndpoint(musicEndpointUrl.data()) << '\n'
		<< "sam.endpoint=" << diagnosticEndpoint(segmentationEndpointUrl.data()) << '\n'
		<< "task.busy=" << (busy ? "yes" : "no") << '\n'
		<< "task.media_busy=" << (mediaBusy ? "yes" : "no") << '\n'
		<< "task.queued=" << (deferredTask == DeferredTask::None
			? "none" : deferredTaskHistoryKind(deferredTask)) << '\n'
		<< "documents.loaded=" << loadedDocumentSources.size() << '\n'
		<< "history.entries=" << jobHistory.entries().size() << '\n';
	const auto runtime = [&report](const char * id,
		const ofxICExample::ManagedProcess & process,
		const std::string & executableState, const std::string & modelState = {}) {
		report << "runtime." << id << ".state=" <<
			ofxICExample::managedProcessStateLabel(process.state()) << '\n'
			<< "runtime." << id << ".ownership=" <<
			(process.ownsProcess() ? "owned" :
				(process.state() == ofxICExample::ManagedProcessState::Ready ? "external" : "none")) << '\n'
			<< "runtime." << id << ".pid=" << process.processId() << '\n'
			<< "runtime." << id << ".launch=" << process.launchMethod() << '\n'
			<< "runtime." << id << ".executable=" << executableState << '\n'
			<< "runtime." << id << ".recent_output=" <<
				(process.recentOutput().empty() ? "no" : "yes") << '\n';
		if (!modelState.empty()) report << "runtime." << id << ".model=" << modelState << '\n';
	};
	runtime("llama", llamaProcess,
		fileState(llamaServerPath.data()), fileState(llamaModelPath.data()));
	runtime("stable_diffusion", stableDiffusionProcess,
		fileState(stableDiffusionServerPath.data()), fileState(stableDiffusionModelPath.data()));
	runtime("ace_step", aceStepProcess,
		fileState(aceStepServerPath.data()), isNativeAceStepServer(aceStepServerPath.data())
			? (!configuredPath(aceStepModelDirectory.data()) ? "not-configured" :
				(hasNativeAceStepModels(aceStepModelDirectory.data())
					? "visible-gguf-set" : "configured-unverified-gguf-set"))
			: "python-checkpoints");
	runtime("whisper", whisperProcess,
		fileState(whisperServerPath.data()), fileState(whisperModelPath.data()));
	runtime("sam", samBridgeProcess,
		fileState(samBridgeExecutablePath.data()), fileState(samModelPath.data()));
	return report.str();
}

bool ofApp::exportDiagnostics(const std::string & path) {
	if (path.empty()) {
		diagnosticsStatus = "Diagnostic export cancelled.";
		return false;
	}
	std::string error;
	if (!ofxICExample::writeTextFileAtomically(path, diagnosticsReport(), &error)) {
		diagnosticsStatus = "Could not export diagnostics: " + error;
		ofLogError("ofxIC diagnostics") << diagnosticsStatus;
		return false;
	}
	diagnosticsStatus = "Privacy-aware diagnostics exported to " + path;
	ofLogNotice("ofxIC diagnostics") << diagnosticsStatus;
	return true;
}

void ofApp::startLocalStableDiffusionServer() {
	const std::string requestedSignature = stableDiffusionRuntimeSignature();
	if (stableDiffusionProcess.running()) {
		if (!stableDiffusionActiveRuntimeSignature.empty() &&
			stableDiffusionActiveRuntimeSignature != requestedSignature &&
			stableDiffusionProcess.ownsProcess()) {
			stopLocalStableDiffusionServer();
		} else return;
	}
	if (stableDiffusionProcess.useExisting("sd-server", 8081)) {
		stableDiffusionProcess.followOutputFiles(localServerLogFiles("sd-server", 8081));
		stableDiffusionActiveModelPath.clear();
		stableDiffusionActiveRuntimeSignature.clear();
		setTextBuffer(mediaEndpointUrl, "http://127.0.0.1:8081");
		mediaConfigurationDirty = true;
		stableDiffusionServerStatus = stableDiffusionProcess.status() +
			" The model selector cannot change this external process.";
		return;
	}
	detectedStableDiffusionServerPath = ofxICExample::resolveInstalledExecutable(
		stableDiffusionServerPath.data(), detectedStableDiffusionServerPath,
		installedServerRoot(),
		"stable-diffusion.cpp-", "sd-server.exe");
	if (detectedStableDiffusionServerPath.empty()) {
		const std::string configured = stableDiffusionServerPath.data();
		stableDiffusionServerStatus = configured.empty()
			? "sd-server executable was not found under " + installedServerRoot() + "."
			: "sd-server executable was not found: " + configured;
		ofLogError("ofxIC servers") << stableDiffusionServerStatus << "\n" <<
			runtimeExecutableFailureDiagnostic(configured,
				detectedStableDiffusionServerPath, "stable-diffusion.cpp-", "sd-server.exe");
		return;
	}
	setTextBuffer(stableDiffusionServerPath, detectedStableDiffusionServerPath);
	if (!configuredPath(stableDiffusionModelPath.data())) {
		stableDiffusionServerStatus = "Choose a Stable Diffusion model before starting sd-server.";
		return;
	}
	std::vector<std::string> arguments{ stableDiffusionCompleteCheckpoint ? "--model" : "--diffusion-model",
		stableDiffusionModelPath.data(), "--backend", "cuda0", "--listen-ip", "127.0.0.1", "--listen-port", "8081" };
	if (!stableDiffusionCompleteCheckpoint && stableDiffusionVaePath[0]) arguments.insert(arguments.end(), { "--vae", stableDiffusionVaePath.data() });
	if (!stableDiffusionCompleteCheckpoint && stableDiffusionClipLPath[0]) arguments.insert(arguments.end(), { "--clip_l", stableDiffusionClipLPath.data() });
	if (!stableDiffusionCompleteCheckpoint && stableDiffusionClipGPath[0]) arguments.insert(arguments.end(), { "--clip_g", stableDiffusionClipGPath.data() });
	if (!stableDiffusionCompleteCheckpoint && stableDiffusionTextEncoderPath[0]) arguments.insert(arguments.end(), { "--t5xxl", stableDiffusionTextEncoderPath.data() });
	if (stableDiffusionFlashAttention) arguments.push_back("--diffusion-fa");
	if (stableDiffusionOffloadToCpu) arguments.push_back("--offload-to-cpu");
	if (stableDiffusionProcess.start(
		stableDiffusionServerPath.data(), arguments, "sd-server", 8081)) {
		stableDiffusionActiveModelPath = stableDiffusionModelPath.data();
		stableDiffusionActiveRuntimeSignature = requestedSignature;
		setTextBuffer(mediaEndpointUrl, "http://127.0.0.1:8081");
		mediaConfigurationDirty = true;
	}
	stableDiffusionServerStatus = stableDiffusionProcess.status();
}

void ofApp::stopLocalStableDiffusionServer() {
	stableDiffusionProcess.stop();
	stableDiffusionActiveModelPath.clear();
	stableDiffusionActiveRuntimeSignature.clear();
	stableDiffusionServerStatus = stableDiffusionProcess.status();
}

std::string ofApp::stableDiffusionRuntimeSignature() const {
	return ofxICExample::mediaRuntimeSignature({
		stableDiffusionServerPath.data(), stableDiffusionModelPath.data(),
		stableDiffusionVaePath.data(), stableDiffusionClipLPath.data(),
		stableDiffusionClipGPath.data(), stableDiffusionTextEncoderPath.data(),
		stableDiffusionCompleteCheckpoint, stableDiffusionFlashAttention,
		stableDiffusionOffloadToCpu });
}

void ofApp::startLocalAceStepServer() {
	if (aceStepProcess.running()) return;
	if (aceStepProcess.useExisting("ACE-Step server", 8085)) {
		setTextBuffer(musicEndpointUrl, "http://127.0.0.1:8085");
		musicConfigurationDirty = true;
		aceStepServerStatus = aceStepProcess.status();
		return;
	}
	std::string resolved = ofxICExample::resolveInstalledExecutable(
		aceStepServerPath.data(), detectedAceStepServerPath, installedServerRoot(),
		"acestep.cpp-", "ace-server.exe");
	if (resolved.empty()) {
		resolved = ofxICExample::resolveInstalledExecutable(
			aceStepServerPath.data(), detectedAceStepServerPath, installedServerRoot(),
			"ACE-Step-1.5-", "python.exe");
	}
	if (resolved.empty()) {
		aceStepServerStatus = "ACE-Step server not found. Run scripts\\install-acestep-server.ps1.";
		ofLogError("ofxIC servers") << aceStepServerStatus << "\nNative: " <<
			runtimeExecutableFailureDiagnostic(aceStepServerPath.data(),
				detectedAceStepServerPath, "acestep.cpp-", "ace-server.exe") <<
			"\nPython fallback: " << runtimeExecutableFailureDiagnostic(
				aceStepServerPath.data(), detectedAceStepServerPath,
				"ACE-Step-1.5-", "python.exe");
		return;
	}
	setTextBuffer(aceStepServerPath, resolved);
	if (isNativeAceStepServer(resolved) && !configuredPath(aceStepModelDirectory.data())) {
		aceStepServerStatus = "Choose the ACE-Step GGUF model folder before starting the server.";
		ofLogError("ofxIC servers") << aceStepServerStatus;
		return;
	}
	if (managedAceStepArguments(aceStepServerArguments.data()))
		setTextBuffer(aceStepServerArguments, defaultAceStepArguments(
			resolved, aceStepModelDirectory.data()));
#if defined(_WIN32)
	const auto arguments = splitWindowsArguments(aceStepServerArguments.data());
#else
	const std::vector<std::string> arguments;
#endif
	std::filesystem::path workingDirectory = std::filesystem::path(resolved).parent_path();
	if (workingDirectory.filename() == "Scripts" &&
		workingDirectory.parent_path().filename() == ".venv")
		workingDirectory = workingDirectory.parent_path().parent_path();
	ofLogNotice("ofxIC servers") << "Starting ACE-Step 1.5: " << resolved;
	if (aceStepProcess.start(resolved, arguments, "ACE-Step 1.5 server", 8085,
		workingDirectory.string())) {
		setTextBuffer(musicEndpointUrl, "http://127.0.0.1:8085");
		musicConfigurationDirty = true;
	}
	aceStepServerStatus = aceStepProcess.status();
}

void ofApp::stopLocalAceStepServer() {
	aceStepProcess.stop();
	aceStepServerStatus = aceStepProcess.status();
}

void ofApp::startLocalWhisperServer() {
	if (whisperProcess.running()) return;
	if (whisperProcess.useExisting("whisper.cpp server", 8082)) {
		setTextBuffer(transcriptionEndpointUrl, "http://127.0.0.1:8082");
		whisperServerStatus = whisperProcess.status();
		return;
	}
	const std::string resolved = ofxICExample::resolveInstalledExecutable(
		whisperServerPath.data(), detectedWhisperServerPath, installedServerRoot(),
		"whisper.cpp-", "whisper-server.exe");
	if (resolved.empty()) {
		whisperServerStatus = "whisper-server not found. Run scripts\\install-whisper-server.ps1.";
		ofLogError("ofxIC servers") << whisperServerStatus << "\n" <<
			runtimeExecutableFailureDiagnostic(whisperServerPath.data(),
				detectedWhisperServerPath, "whisper.cpp-", "whisper-server.exe");
		return;
	}
	setTextBuffer(whisperServerPath, resolved);
	if (!configuredPath(whisperModelPath.data())) {
		whisperServerStatus = "Choose a whisper.cpp model before starting the server.";
		return;
	}
#if defined(_WIN32)
	auto arguments = splitWindowsArguments(whisperServerArguments.data());
#else
	std::vector<std::string> arguments;
#endif
	arguments.insert(arguments.begin(), { "-m", whisperModelPath.data(), "--host", "127.0.0.1", "--port", "8082" });
	ofLogNotice("ofxIC servers") << "Starting whisper-server: " << resolved;
	if (whisperProcess.start(resolved, arguments, "whisper.cpp server", 8082)) {
		setTextBuffer(transcriptionEndpointUrl, "http://127.0.0.1:8082");
	}
	whisperServerStatus = whisperProcess.status();
}

void ofApp::stopLocalWhisperServer() {
	whisperProcess.stop();
	whisperServerStatus = whisperProcess.status();
}

void ofApp::startLocalSamBridge() {
	if (samBridgeProcess.running()) return;
	if (samBridgeProcess.useExisting("SAM bridge", 18085)) {
		setTextBuffer(segmentationEndpointUrl, "http://127.0.0.1:18085");
		samBridgeProcessStatus = samBridgeProcess.status();
		return;
	}
	const std::string installedPython = ofxICExample::resolveInstalledExecutable(
		samBridgeExecutablePath.data(), detectedSamPythonPath, installedServerRoot(),
		"sam-python-", "python.exe");
	if (installedPython.empty()) {
		samBridgeProcessStatus = "SAM Python environment not found. Run scripts\\install-sam-server.ps1.";
		ofLogError("ofxIC servers") << samBridgeProcessStatus << "\n" <<
			runtimeExecutableFailureDiagnostic(samBridgeExecutablePath.data(),
				detectedSamPythonPath, "sam-python-", "python.exe");
		return;
	}
	setTextBuffer(samBridgeExecutablePath, installedPython);
	const std::string bridge = bundledSamBridgeScript();
	if (bridge.empty()) {
		samBridgeProcessStatus = "Bundled scripts\\sam-bridge-server.py was not found.";
		ofLogError("ofxIC servers") << samBridgeProcessStatus;
		return;
	}
	const std::string installedRunner = ofxICExample::resolveInstalledExecutable(
		samRunnerPath.data(), detectedSamRunnerPath, installedServerRoot(),
		"sam-python-", "sam-python-runner.py");
	if (installedRunner.empty()) {
		samBridgeProcessStatus = "SAM runner not found. Run scripts\\install-sam-server.ps1.";
		ofLogError("ofxIC servers") << samBridgeProcessStatus << "\n" <<
			runtimeExecutableFailureDiagnostic(samRunnerPath.data(),
				detectedSamRunnerPath, "sam-python-", "sam-python-runner.py");
		return;
	}
	setTextBuffer(samRunnerPath, installedRunner);
	if (!configuredPath(samModelPath.data())) {
		samBridgeProcessStatus = "Choose a Meta SAM checkpoint before starting the bridge.";
		ofLogError("ofxIC servers") << samBridgeProcessStatus;
		return;
	}
#if defined(_WIN32)
	std::vector<std::string> arguments{
		bridge, "--host", "127.0.0.1", "--port", "18085",
		"--adapter", installedRunner, "--model", samModelPath.data(),
		"--backend", samCuda ? "cuda" : "cpu" };
	const auto extras = splitWindowsArguments(samBridgeArguments.data());
	arguments.insert(arguments.end(), extras.begin(), extras.end());
#else
	const std::vector<std::string> arguments;
#endif
	if (samBridgeProcess.start(
		installedPython, arguments, "SAM bridge", 18085,
		std::filesystem::path(bridge).parent_path().string())) {
		setTextBuffer(segmentationEndpointUrl, "http://127.0.0.1:18085");
	}
	samBridgeProcessStatus = samBridgeProcess.status();
}

void ofApp::stopLocalSamBridge() {
	samBridgeProcess.stop();
	samBridgeProcessStatus = samBridgeProcess.status();
}

bool ofApp::loadDocument(const std::string & path) {
	const std::string source = documentSourceName(path);
	if (source.empty() || !supportedDocumentPath(path)) {
		documentStatus = "Rejected " + (source.empty() ? std::string("document") : source) +
			": only .md and .txt files are accepted.";
		writeDocumentAutomationResult(documentStatus, loadedDocumentSources);
		return false;
	}
	if (!documents.addFile(path, source)) {
		documentStatus = "Could not load " + source +
			": it is missing, unreadable, empty, already loaded, or exceeds index limits.";
		writeDocumentAutomationResult(documentStatus, loadedDocumentSources);
		return false;
	}
	ofBuffer documentBuffer = ofBufferFromFile(path, true);
	loadedDocumentSources.push_back(source);
	loadedDocumentContents.push_back(documentBuffer.getText());
	selectedDocument = static_cast<int>(loadedDocumentSources.size() - 1);
	documentStatus = "Loaded " + source;
	writeDocumentAutomationResult(documentStatus, loadedDocumentSources);
	return true;
}

void ofApp::applySettingsToUi(const ofxICExample::ExampleSettings & settings) {
	const std::string workbenchRoot = ofFilePath::getCurrentExeDir();
	const auto resolveWorkbenchPath = [&](const std::string & value) {
		return ofxICExample::resolveWorkbenchPath(value, workbenchRoot);
	};
	selectedProfile = settings.endpointProfile;
	selectedMediaBackend = settings.mediaBackend;
	selectedMusicBackend = settings.musicBackend;
	selectedMediaKind = supportsMediaKind(settings.mediaBackend, settings.mediaKind)
		? settings.mediaKind
		: 0;
	setTextBuffer(endpointUrl, settings.endpointUrl);
	setTextBuffer(modelId, settings.modelId);
	setTextBuffer(chatSystemPrompt, settings.chatSystemPrompt);
	setTextBuffer(chatStopSequences, settings.chatStopSequences);
	chatMaxTokens = settings.chatMaxTokens;
	chatTemperature = settings.chatTemperature;
	chatTopP = settings.chatTopP;
	chatSeed = settings.chatSeed;
	setTextBuffer(llamaServerPath, resolveWorkbenchPath(settings.llamaServerPath));
	setTextBuffer(llamaModelPath, resolveWorkbenchPath(settings.llamaModelPath));
	setTextBuffer(llamaModelDirectory, resolveWorkbenchPath(settings.llamaModelDirectory));
	llamaContextSize = settings.llamaContextSize;
	llamaGpuLayers = settings.llamaGpuLayers;
	llamaFlashAttention = settings.llamaFlashAttention != 0;
	detectedLlamaModels = detectedLlamaModelPaths(llamaModelDirectory.data());
	setTextBuffer(transcriptionEndpointUrl, settings.transcriptionEndpointUrl);
	transcriptionProtocol = settings.transcriptionProtocol;
	setTextBuffer(transcriptionModel, settings.transcriptionModel);
	setTextBuffer(segmentationEndpointUrl, settings.segmentationEndpointUrl);
	setTextBuffer(mediaEndpointUrl, settings.mediaEndpointUrl);
	setTextBuffer(mediaImageModel, settings.mediaImageModel);
	setTextBuffer(mediaVideoModel, settings.mediaVideoModel);
	setTextBuffer(stableDiffusionModelDirectory,
		resolveWorkbenchPath(settings.stableDiffusionModelDirectory));
	setTextBuffer(stableDiffusionServerPath,
		resolveWorkbenchPath(settings.stableDiffusionServerPath));
	setTextBuffer(stableDiffusionModelPath,
		resolveWorkbenchPath(settings.stableDiffusionModelPath));
	setTextBuffer(stableDiffusionVaePath,
		resolveWorkbenchPath(settings.stableDiffusionVaePath));
	setTextBuffer(stableDiffusionTextEncoderPath,
		resolveWorkbenchPath(settings.stableDiffusionTextEncoderPath));
	setTextBuffer(stableDiffusionClipLPath,
		resolveWorkbenchPath(settings.stableDiffusionClipLPath));
	setTextBuffer(stableDiffusionClipGPath,
		resolveWorkbenchPath(settings.stableDiffusionClipGPath));
	stableDiffusionCompleteCheckpoint = settings.stableDiffusionCompleteCheckpoint != 0;
	stableDiffusionFlashAttention = settings.stableDiffusionFlashAttention != 0;
	stableDiffusionOffloadToCpu = settings.stableDiffusionOffloadToCpu != 0;
	if (ofToLower(ofFilePath::getFileExt(stableDiffusionModelPath.data())) == "gguf")
		stableDiffusionCompleteCheckpoint = false;
	applyInferredMediaKind(stableDiffusionModelPath.data());
	scanStableDiffusionModels(stableDiffusionModelDirectory.data(), detectedDiffusionModels,
		detectedVaeModels, detectedTextEncoders);
	mediaWidth = settings.mediaWidth;
	mediaHeight = settings.mediaHeight;
	mediaFrames = settings.mediaFrames;
	mediaFps = settings.mediaFps;
	mediaSeed = settings.mediaSeed;
	mediaSteps = settings.mediaSteps;
	mediaGuidance = settings.mediaGuidance;
	mediaSampler = settings.mediaSampler;
	mediaScheduler = settings.mediaScheduler;
	mediaOutputFormat = settings.mediaOutputFormat;
	setTextBuffer(musicEndpointUrl, settings.musicEndpointUrl);
	musicDuration = settings.musicDuration;
	musicOutputFormat = settings.musicOutputFormat;
	setTextBuffer(aceStepServerPath, resolveWorkbenchPath(settings.aceStepServerPath));
	setTextBuffer(aceStepServerArguments, settings.aceStepServerArguments);
	setTextBuffer(aceStepModelDirectory,
		resolveWorkbenchPath(settings.aceStepModelDirectory));
	setTextBuffer(whisperServerPath, resolveWorkbenchPath(settings.whisperServerPath));
	setTextBuffer(whisperModelPath, resolveWorkbenchPath(settings.whisperModelPath));
	setTextBuffer(whisperServerArguments, settings.whisperServerArguments);
	setTextBuffer(samBridgeExecutablePath,
		resolveWorkbenchPath(settings.samBridgeExecutablePath));
	setTextBuffer(samBridgeArguments, settings.samBridgeArguments);
	setTextBuffer(samRunnerPath, resolveWorkbenchPath(settings.samRunnerPath));
	setTextBuffer(samModelPath, resolveWorkbenchPath(settings.samModelPath));
	samCuda = settings.samCuda != 0;
	configurationDirty = false;
	mediaConfigurationDirty = false;
	musicConfigurationDirty = false;
}

ofxICExample::ExampleSettings ofApp::settingsFromUi() const {
	const std::string workbenchRoot = ofFilePath::getCurrentExeDir();
	const auto portableWorkbenchPath = [&](const std::string & value) {
		return ofxICExample::portableWorkbenchPath(value, workbenchRoot);
	};
	ofxICExample::ExampleSettings settings;
	settings.endpointProfile = selectedProfile;
	settings.endpointUrl = endpointUrl.data();
	settings.modelId = modelId.data();
	settings.chatSystemPrompt = chatSystemPrompt.data();
	settings.chatStopSequences = chatStopSequences.data();
	settings.chatMaxTokens = chatMaxTokens;
	settings.chatTemperature = chatTemperature;
	settings.chatTopP = chatTopP;
	settings.chatSeed = chatSeed;
	settings.llamaServerPath = portableWorkbenchPath(llamaServerPath.data());
	settings.llamaModelPath = portableWorkbenchPath(llamaModelPath.data());
	settings.llamaModelDirectory = portableWorkbenchPath(llamaModelDirectory.data());
	settings.llamaContextSize = llamaContextSize;
	settings.llamaGpuLayers = llamaGpuLayers;
	settings.llamaFlashAttention = llamaFlashAttention ? 1 : 0;
	settings.transcriptionEndpointUrl = transcriptionEndpointUrl.data();
	settings.transcriptionProtocol = transcriptionProtocol;
	settings.transcriptionModel = transcriptionModel.data();
	settings.segmentationEndpointUrl = segmentationEndpointUrl.data();
	settings.mediaBackend = selectedMediaBackend;
	settings.mediaKind = selectedMediaKind;
	settings.mediaEndpointUrl = mediaEndpointUrl.data();
	settings.mediaImageModel = mediaImageModel.data();
	settings.mediaVideoModel = mediaVideoModel.data();
	settings.stableDiffusionModelDirectory =
		portableWorkbenchPath(stableDiffusionModelDirectory.data());
	settings.stableDiffusionServerPath =
		portableWorkbenchPath(stableDiffusionServerPath.data());
	settings.stableDiffusionModelPath =
		portableWorkbenchPath(stableDiffusionModelPath.data());
	settings.stableDiffusionVaePath = portableWorkbenchPath(stableDiffusionVaePath.data());
	settings.stableDiffusionTextEncoderPath =
		portableWorkbenchPath(stableDiffusionTextEncoderPath.data());
	settings.stableDiffusionClipLPath =
		portableWorkbenchPath(stableDiffusionClipLPath.data());
	settings.stableDiffusionClipGPath =
		portableWorkbenchPath(stableDiffusionClipGPath.data());
	settings.stableDiffusionCompleteCheckpoint = stableDiffusionCompleteCheckpoint ? 1 : 0;
	settings.stableDiffusionFlashAttention = stableDiffusionFlashAttention ? 1 : 0;
	settings.stableDiffusionOffloadToCpu = stableDiffusionOffloadToCpu ? 1 : 0;
	settings.mediaWidth = mediaWidth;
	settings.mediaHeight = mediaHeight;
	settings.mediaFrames = mediaFrames;
	settings.mediaFps = mediaFps;
	settings.mediaSeed = mediaSeed;
	settings.mediaSteps = mediaSteps;
	settings.mediaGuidance = mediaGuidance;
	settings.mediaSampler = mediaSampler;
	settings.mediaScheduler = mediaScheduler;
	settings.mediaOutputFormat = mediaOutputFormat;
	settings.musicBackend = selectedMusicBackend;
	settings.musicEndpointUrl = musicEndpointUrl.data();
	settings.musicDuration = musicDuration;
	settings.musicOutputFormat = musicOutputFormat;
	settings.aceStepServerPath = portableWorkbenchPath(aceStepServerPath.data());
	settings.aceStepServerArguments = aceStepServerArguments.data();
	settings.aceStepModelDirectory = portableWorkbenchPath(aceStepModelDirectory.data());
	settings.whisperServerPath = portableWorkbenchPath(whisperServerPath.data());
	settings.whisperModelPath = portableWorkbenchPath(whisperModelPath.data());
	settings.whisperServerArguments = whisperServerArguments.data();
	settings.samBridgeExecutablePath = portableWorkbenchPath(samBridgeExecutablePath.data());
	settings.samBridgeArguments = samBridgeArguments.data();
	settings.samRunnerPath = portableWorkbenchPath(samRunnerPath.data());
	settings.samModelPath = portableWorkbenchPath(samModelPath.data());
	settings.samCuda = samCuda ? 1 : 0;
	return settings;
}

void ofApp::saveExampleSettings() {
	if (ofxICExample::saveSettings(settingsPath, settingsFromUi())) {
		settingsStatus = "Saved non-secret settings. Paths inside the Workbench are portable relative paths.";
	} else {
		settingsStatus = "Could not save settings; check values and file access.";
	}
}

void ofApp::resetExampleSettings() {
	const bool removed = ofxICExample::removeSettings(settingsPath);
	if (!removed) {
		settingsStatus = "Could not remove the saved settings file.";
		return;
	}
	ofxICExample::ExampleSettings settings;
	const auto environment = settingsEnvironment();
	ofxICExample::applyEnvironmentOverrides(settings, environment);
	applySettingsToUi(settings);
	applyLocalRuntimeDefaults();
	applyConfiguration();
	applyMediaConfiguration();
	applyMusicConfiguration();
	settingsStatus = environment.empty()
		? "Restored built-in defaults and removed saved settings."
		: "Removed saved settings; environment overrides remain active.";
}

void ofApp::applyConfiguration() {
	if (busy || mediaBusy) return;
	const auto validation = ofxIC::Endpoint::validateBaseUrl(endpointUrl.data());
	endpoint.setBaseUrl(validation ? validation.normalizedUrl : std::string(endpointUrl.data()));
	if (!validation) {
		status = "Invalid LLM endpoint: " + validation.error;
		ofLogError("ofxIC configuration") << status;
		return;
	}
	setTextBuffer(endpointUrl, validation.normalizedUrl);
	endpoint.setBearerToken(configuredToken());
	ofxIC::ChatOptions options = chat.getOptions();
	options.model = modelId.data();
	options.maxTokens = std::clamp(chatMaxTokens, 1, 131072);
	options.temperature = std::clamp(chatTemperature, 0.0f, 2.0f);
	options.topP = std::clamp(chatTopP, 0.0f, 1.0f);
	options.seed = chatSeed;
	options.stopSequences = nonEmptyLines(chatStopSequences.data());
	chat.setOptions(options);
	chat.setSystemPrompt(chatSystemPrompt.data());
	chat.clear();
	availableModels.clear();
	lastMessage.clear();
	output.clear();
	configurationDirty = false;
	status = "Applied " + std::string(endpointProfiles[selectedProfile].name) +
		" at " + endpoint.getBaseUrl();
	if (!validation.secure && !validation.loopback)
		status += " (warning: remote HTTP is unencrypted)";
}

void ofApp::applyMediaConfiguration() {
	if (busy || mediaBusy) return;
	const auto validation = ofxIC::Endpoint::validateBaseUrl(mediaEndpointUrl.data());
	mediaEndpoint.setBaseUrl(validation ? validation.normalizedUrl :
		std::string(mediaEndpointUrl.data()));
	if (!validation) {
		mediaStatus = "Invalid media endpoint: " + validation.error;
		ofLogError("ofxIC configuration") << mediaStatus;
		return;
	}
	setTextBuffer(mediaEndpointUrl, validation.normalizedUrl);
	mediaEndpoint.setBearerToken(configuredMediaToken());
	mediaConfigurationDirty = false;
	currentMediaJob = {};
	mediaStatus = "Applied " + std::string(mediaBackends[selectedMediaBackend].name) +
		" at " + mediaEndpoint.getBaseUrl();
	if (!validation.secure && !validation.loopback)
		mediaStatus += " (warning: remote HTTP is unencrypted)";
}

void ofApp::selectEndpointProfile(int profileIndex) {
	if (profileIndex < 0 || profileIndex >= static_cast<int>(endpointProfiles.size())) return;
	selectedProfile = profileIndex;
	const EndpointProfile & profile = endpointProfiles[selectedProfile];
	if (*profile.url) {
		setTextBuffer(endpointUrl, profile.url);
		setTextBuffer(modelId, "");
	}
	configurationDirty = true;
	applyConfiguration();
}

void ofApp::selectMediaBackend(int backendIndex) {
	if (backendIndex < 0 || backendIndex >= static_cast<int>(mediaBackends.size())) return;
	selectedMediaBackend = backendIndex;
	if (!supportsMediaKind(selectedMediaBackend, selectedMediaKind)) {
		selectedMediaKind = mediaBackends[selectedMediaBackend].supportsImage ? 0 : 1;
	}
	setTextBuffer(mediaEndpointUrl, mediaBackends[selectedMediaBackend].url);
	const std::string imageModel(mediaImageModel.data());
	if (selectedMediaBackend == 0) {
		if (imageModel.empty() || imageModel == "black-forest-labs/FLUX.1-dev") {
			setTextBuffer(mediaImageModel, "gpt-image-2");
		}
		const bool supportedGptImageSize =
			(mediaWidth == 1024 && mediaHeight == 1024) ||
			(mediaWidth == 1024 && mediaHeight == 1536) ||
			(mediaWidth == 1536 && mediaHeight == 1024);
		if (!supportedGptImageSize) {
			mediaWidth = 1024;
			mediaHeight = 1024;
		}
	} else if (selectedMediaBackend == 1 &&
		(imageModel.empty() || imageModel.compare(0, 9, "gpt-image") == 0)) {
		setTextBuffer(mediaImageModel, "black-forest-labs/FLUX.1-dev");
	}
	mediaConfigurationDirty = true;
	applyMediaConfiguration();
}

void ofApp::applyInferredMediaKind(const std::string & modelPath) {
	const auto inferred = ofxICExample::inferMediaModelKind(modelPath);
	if (!inferred) return;
	selectedMediaKind = *inferred == ofxICExample::MediaModelKind::Video ? 1 : 0;
}

void ofApp::selectMusicBackend(int backendIndex) {
	if (backendIndex < 0 || backendIndex >= static_cast<int>(musicBackends.size())) return;
	selectedMusicBackend = backendIndex;
	setTextBuffer(musicEndpointUrl, musicBackends[selectedMusicBackend].url);
	if (selectedMusicBackend == 1) musicDuration = std::min(musicDuration, 380);
	musicConfigurationDirty = true;
	applyMusicConfiguration();
}

std::string ofApp::configuredToken() const {
	const std::string generic = environmentValue("OFXIC_API_KEY");
	if (!generic.empty()) return generic;
	const std::string variable = endpointProfiles[selectedProfile].tokenEnvironment;
	const std::string provider = environmentValue(variable.c_str());
	if (!provider.empty()) return provider;
	const auto stored = storedTokens.find(variable);
	if (stored != storedTokens.end()) return stored->second;
	const auto storedGeneric = storedTokens.find("OFXIC_API_KEY");
	return storedGeneric == storedTokens.end() ? std::string{} : storedGeneric->second;
}

void ofApp::applyMusicConfiguration() {
	if (busy || mediaBusy) return;
	const auto validation = ofxIC::Endpoint::validateBaseUrl(musicEndpointUrl.data());
	musicEndpoint.setBaseUrl(validation ? validation.normalizedUrl :
		std::string(musicEndpointUrl.data()));
	if (!validation) {
		musicStatus = "Invalid music endpoint: " + validation.error;
		ofLogError("ofxIC configuration") << musicStatus;
		return;
	}
	setTextBuffer(musicEndpointUrl, validation.normalizedUrl);
	musicEndpoint.setBearerToken(selectedMusicBackend == 1 ? configuredMusicToken() : "");
	musicConfigurationDirty = false;
	currentMusicJob = {};
	currentAceStepMusicJob = {};
	musicStatus = "Applied " + std::string(musicBackends[selectedMusicBackend].name) +
		" at " + musicEndpoint.getBaseUrl();
	if (!validation.secure && !validation.loopback)
		musicStatus += " (warning: remote HTTP is unencrypted)";
}

bool ofApp::loadAudio(const std::string & path) {
	const std::string extension = ofToLower(ofFilePath::getFileExt(path));
	if (extension != "wav" && extension != "mp3" && extension != "m4a" &&
		extension != "ogg" && extension != "flac" && extension != "webm" &&
		extension != "mp4" && extension != "mpeg" && extension != "mpga") {
		audioStatus = "Rejected audio: use wav, mp3, m4a, ogg, flac, webm, mp4, mpeg, or mpga.";
		return false;
	}
	const ofBuffer buffer = ofBufferFromFile(path, true);
	if (buffer.size() == 0) {
		audioStatus = "Could not load audio, or the file is empty.";
		return false;
	}
	audioBytes.assign(buffer.getData(), buffer.size());
	audioFilename = ofFilePath::getFileName(path);
	audioStatus = "Loaded " + audioFilename + " (" + ofToString(buffer.size()) + " bytes).";
	return true;
}

void ofApp::transcribeAudio() {
	if (audioBytes.empty() || mediaBusy || busy) return;
	if (transcriptionProtocol == 1 && deferUntilRuntimeReady(DeferredTask::Transcription)) return;
	const auto validation = ofxIC::Endpoint::validateBaseUrl(transcriptionEndpointUrl.data());
	transcriptionEndpoint.setBaseUrl(validation ? validation.normalizedUrl :
		std::string(transcriptionEndpointUrl.data()));
	if (!validation) {
		audioStatus = "Invalid transcription endpoint: " + validation.error;
		status = audioStatus;
		writeAutomationResult(audioStatus, "");
		return;
	}
	setTextBuffer(transcriptionEndpointUrl, validation.normalizedUrl);
	if (busy.exchange(true)) return;
	activeTaskKind = "transcription";
	transcriptionEndpoint.setBearerToken(configuredTranscriptionToken());
	cancellationRequested = false;
	requestCanCancel = true;
	status = "Transcribing audio...";
	ofxIC::TranscriptionRequest request;
	request.audioBytes = audioBytes;
	request.filename = audioFilename;
	request.model = transcriptionModel[0] ? transcriptionModel.data() : "whisper-1";
	const std::string extension = ofToLower(ofFilePath::getFileExt(audioFilename));
	request.contentType = extension == "mp3" ? "audio/mpeg"
		: extension == "ogg" ? "audio/ogg"
		: extension == "webm" ? "audio/webm"
		: extension == "flac" ? "audio/flac"
		: "audio/wav";
	const int protocol = transcriptionProtocol;
	const auto currentModels = availableModels;
	worker = std::thread([this, request = std::move(request), protocol, currentModels]() {
		ofxIC::RequestControl control;
		control.shouldCancel = [this]() { return cancellationRequested.load(); };
		const auto result = protocol == 0
			? transcription.transcribeOpenAI(request, control)
			: transcription.transcribeWhisperCpp(request, control);
		std::lock_guard<std::mutex> lock(resultMutex);
		pendingOutput = result.text;
		pendingStatus = result
			? "Transcription completed"
			: result.failure == ofxIC::RequestFailure::Cancelled ? "Transcription cancelled"
			: result.failure == ofxIC::RequestFailure::Timeout ? "Transcription timed out"
			: "Transcription failed: " + result.error;
		pendingModels = currentModels;
		finished = true;
	});
}

bool ofApp::loadSegmentationImage(const std::string & path) {
	ofImage loaded;
	if (!loaded.load(path) || !loaded.isAllocated()) {
		segmentationStatus = "Could not load segmentation image.";
		return false;
	}
	const auto & pixels = loaded.getPixels();
	if (pixels.getWidth() == 0 || pixels.getHeight() == 0) {
		segmentationStatus = "Segmentation image has no pixels.";
		return false;
	}
	std::string ppm = "P6\n" + ofToString(pixels.getWidth()) + " " +
		ofToString(pixels.getHeight()) + "\n255\n";
	ppm.reserve(ppm.size() + pixels.getWidth() * pixels.getHeight() * 3U);
	for (std::size_t y = 0; y < pixels.getHeight(); ++y) {
		for (std::size_t x = 0; x < pixels.getWidth(); ++x) {
			const ofColor color = pixels.getColor(x, y);
			ppm.push_back(static_cast<char>(color.r));
			ppm.push_back(static_cast<char>(color.g));
			ppm.push_back(static_cast<char>(color.b));
		}
	}
	segmentationImage = std::move(loaded);
	segmentationMaskImage.clear();
	segmentationPoints.clear();
	segmentationImageBytes = std::move(ppm);
	segmentationFilename = ofFilePath::getBaseName(path) + ".ppm";
	segmentationStatus = "Loaded " + ofFilePath::getFileName(path) + ".";
	return true;
}

void ofApp::inspectSegmentationBridge() {
	if (mediaBusy || busy) return;
	if (usesManagedSamBridge(segmentationEndpointUrl.data()) &&
		deferUntilRuntimeReady(DeferredTask::SamInspect)) return;
	const auto validation = ofxIC::Endpoint::validateBaseUrl(segmentationEndpointUrl.data());
	segmentationEndpoint.setBaseUrl(validation ? validation.normalizedUrl :
		std::string(segmentationEndpointUrl.data()));
	if (!validation) {
		segmentationStatus = "Invalid SAM endpoint: " + validation.error;
		status = segmentationStatus;
		writeAutomationResult(segmentationStatus, "");
		return;
	}
	setTextBuffer(segmentationEndpointUrl, validation.normalizedUrl);
	if (busy.exchange(true)) return;
	activeTaskKind = "sam-inspection";
	segmentationEndpoint.setBearerToken(configuredSegmentationToken());
	cancellationRequested = false;
	requestCanCancel = true;
	status = "Checking SAM bridge...";
	segmentationStatus = status;
	const std::string currentOutput = output;
	const std::vector<std::string> currentModels = availableModels;
	worker = std::thread([this, currentOutput, currentModels]() {
		const auto bridge = segmentation.inspectSamBridge([this]() {
			return cancellationRequested.load();
		});
		std::lock_guard<std::mutex> lock(resultMutex);
		if (bridge.cancelled) {
			pendingStatus = "Segmentation bridge check cancelled";
		} else if (!bridge) {
			pendingStatus = "Segmentation bridge unavailable: " + bridge.error;
		} else {
			pendingStatus = "Segmentation bridge ready: v" + bridge.version +
				" / " + (bridge.mode.empty() ? "unknown mode" : bridge.mode) +
				" / " + (bridge.backend.empty() ? "unknown backend" : bridge.backend);
		}
		pendingOutput = currentOutput;
		pendingModels = currentModels;
		finished = true;
	});
}

void ofApp::segmentImage() {
	if (segmentationImageBytes.empty() || mediaBusy || busy) return;
	if (usesManagedSamBridge(segmentationEndpointUrl.data()) &&
		deferUntilRuntimeReady(DeferredTask::SamSegment)) return;
	const auto validation = ofxIC::Endpoint::validateBaseUrl(segmentationEndpointUrl.data());
	segmentationEndpoint.setBaseUrl(validation ? validation.normalizedUrl :
		std::string(segmentationEndpointUrl.data()));
	if (!validation) {
		segmentationStatus = "Invalid SAM endpoint: " + validation.error;
		status = segmentationStatus;
		writeAutomationResult(segmentationStatus, "");
		return;
	}
	setTextBuffer(segmentationEndpointUrl, validation.normalizedUrl);
	if (busy.exchange(true)) return;
	activeTaskKind = "segmentation";
	segmentationEndpoint.setBearerToken(configuredSegmentationToken());
	cancellationRequested = false;
	requestCanCancel = true;
	status = "Segmenting image...";
	segmentationStatus = status;
	ofxIC::SegmentationRequest request;
	request.imageBytes = segmentationImageBytes;
	request.filename = segmentationFilename;
	request.points = segmentationPoints;
	if (request.points.empty()) {
		request.points.push_back({ segmentationPointX, segmentationPointY, true });
	}
	const auto currentModels = availableModels;
	worker = std::thread([this, request = std::move(request), currentModels]() {
		const auto result = segmentation.segmentSamBridge(
			request, [this]() { return cancellationRequested.load(); });
		std::lock_guard<std::mutex> lock(resultMutex);
		pendingSegmentationMask = result.maskBytes;
		pendingOutput = result ? "SAM bridge returned a PGM mask." : "";
		pendingStatus = result
			? "Segmentation completed"
			: result.cancelled ? "Segmentation cancelled"
			: "Segmentation failed: " + result.error;
		pendingModels = currentModels;
		finished = true;
	});
}

std::string ofApp::configuredTokenSource() const {
	if (!environmentValue("OFXIC_API_KEY").empty()) return "OFXIC_API_KEY";
	const std::string variable = endpointProfiles[selectedProfile].tokenEnvironment;
	if (!environmentValue(variable.c_str()).empty()) return variable;
	if (storedTokens.count(variable)) return "Windows Credential Manager (" + variable + ")";
	if (storedTokens.count("OFXIC_API_KEY")) {
		return "Windows Credential Manager (OFXIC_API_KEY)";
	}
	return variable;
}

std::string ofApp::configuredTranscriptionToken() const {
	const std::string specific = environmentValue("OFXIC_TRANSCRIPTION_API_KEY");
	if (!specific.empty()) return specific;
	if (transcriptionProtocol == 0) {
		const std::string openAi = environmentValue("OPENAI_API_KEY");
		if (!openAi.empty()) return openAi;
	}
	const std::string generic = environmentValue("OFXIC_API_KEY");
	if (!generic.empty()) return generic;
	if (transcriptionProtocol == 0) {
		const auto storedOpenAi = storedTokens.find("OPENAI_API_KEY");
		if (storedOpenAi != storedTokens.end()) return storedOpenAi->second;
	}
	const auto storedGeneric = storedTokens.find("OFXIC_API_KEY");
	return storedGeneric == storedTokens.end() ? std::string{} : storedGeneric->second;
}

std::string ofApp::configuredSegmentationToken() const {
	const std::string specific = environmentValue("OFXIC_SEGMENTATION_API_KEY");
	if (!specific.empty()) return specific;
	const std::string generic = environmentValue("OFXIC_API_KEY");
	if (!generic.empty()) return generic;
	const auto storedGeneric = storedTokens.find("OFXIC_API_KEY");
	return storedGeneric == storedTokens.end() ? std::string{} : storedGeneric->second;
}

std::string ofApp::configuredMediaToken() const {
	const std::string mediaSpecific = environmentValue("OFXIC_MEDIA_API_KEY");
	if (!mediaSpecific.empty()) return mediaSpecific;
	const std::string providerToken = environmentValue(
		mediaBackends[selectedMediaBackend].tokenEnvironment);
	if (!providerToken.empty()) return providerToken;
	const std::string generic = environmentValue("OFXIC_API_KEY");
	if (!generic.empty()) return generic;
	const std::string variable = mediaBackends[selectedMediaBackend].tokenEnvironment;
	const auto stored = storedTokens.find(variable);
	if (stored != storedTokens.end()) return stored->second;
	const auto storedGeneric = storedTokens.find("OFXIC_API_KEY");
	return storedGeneric == storedTokens.end() ? std::string{} : storedGeneric->second;
}

std::string ofApp::configuredMediaTokenSource() const {
	if (!environmentValue("OFXIC_MEDIA_API_KEY").empty()) return "OFXIC_MEDIA_API_KEY";
	const char * providerEnvironment = mediaBackends[selectedMediaBackend].tokenEnvironment;
	if (!environmentValue(providerEnvironment).empty()) return providerEnvironment;
	if (!environmentValue("OFXIC_API_KEY").empty()) return "OFXIC_API_KEY";
	if (storedTokens.count(providerEnvironment)) {
		return "Windows Credential Manager (" + std::string(providerEnvironment) + ")";
	}
	if (storedTokens.count("OFXIC_API_KEY")) {
		return "Windows Credential Manager (OFXIC_API_KEY)";
	}
	return providerEnvironment;
}

std::string ofApp::configuredMusicToken() const {
	const std::string specific = environmentValue("OFXIC_MUSIC_API_KEY");
	if (!specific.empty()) return specific;
	const std::string stability = environmentValue("STABILITY_API_KEY");
	if (!stability.empty()) return stability;
	const std::string generic = environmentValue("OFXIC_API_KEY");
	if (!generic.empty()) return generic;
	const auto stored = storedTokens.find("STABILITY_API_KEY");
	if (stored != storedTokens.end()) return stored->second;
	const auto storedGeneric = storedTokens.find("OFXIC_API_KEY");
	return storedGeneric == storedTokens.end() ? std::string{} : storedGeneric->second;
}

std::string ofApp::configuredMusicTokenSource() const {
	if (!environmentValue("OFXIC_MUSIC_API_KEY").empty()) return "OFXIC_MUSIC_API_KEY";
	if (!environmentValue("STABILITY_API_KEY").empty()) return "STABILITY_API_KEY";
	if (!environmentValue("OFXIC_API_KEY").empty()) return "OFXIC_API_KEY";
	if (storedTokens.count("STABILITY_API_KEY")) {
		return "Windows Credential Manager (STABILITY_API_KEY)";
	}
	if (storedTokens.count("OFXIC_API_KEY")) {
		return "Windows Credential Manager (OFXIC_API_KEY)";
	}
	return "STABILITY_API_KEY";
}

void ofApp::inspectEndpoint() {
	if (mediaBusy || busy) return;
	if (selectedProfile == 0 && deferUntilRuntimeReady(DeferredTask::LlamaInspect)) return;
	if (busy.exchange(true)) return;
	activeTaskKind = "endpoint-inspection";
	cancellationRequested = false;
	requestCanCancel = true;
	status = "Inspecting endpoint...";
	{
		std::lock_guard<std::mutex> lock(resultMutex);
		pendingProgressStatus.clear();
	}
	const std::string currentOutput = output;
	const std::string currentModel = chat.getOptions().model;
	ofLogNotice("ofxIC inspect") << "route: endpoint=" << endpoint.getBaseUrl()
		<< " model=" << (currentModel.empty() ? "<unspecified>" : currentModel);
	const std::string configuredTimeout =
		environmentValue("OFXIC_INSPECT_TIMEOUT_SECONDS");
	const int timeoutSeconds = configuredTimeout.empty()
		? 0
		: ofToInt(configuredTimeout);
	worker = std::thread([this, currentOutput, currentModel, timeoutSeconds]() {
		ofxIC::RequestControl control;
		control.timeoutSeconds = timeoutSeconds;
		const bool unattendedInspection =
			environmentValue("OFXIC_INSPECT_AUTORUN") == "1" &&
			environmentValue("OFXIC_INSPECT_CANCEL_AFTER_MS").empty();
		if (!unattendedInspection) {
			control.shouldCancel = [this]() { return cancellationRequested.load(); };
		}
		const auto inspection = endpoint.inspect(control);
		std::lock_guard<std::mutex> lock(resultMutex);
		pendingModels = inspection.models;
		pendingModelSelection.clear();
		if (inspection.failure == ofxIC::RequestFailure::Cancelled) {
			pendingStatus = "Inspection cancelled";
		} else if (inspection.failure == ofxIC::RequestFailure::Timeout) {
			pendingStatus = "Inspection timed out";
		} else if (!inspection) {
			pendingStatus = "Inspection failed: " + inspection.error;
		} else if (!currentModel.empty()) {
			pendingStatus = "Endpoint reachable; configured model: " + currentModel +
				" (authentication not tested)";
		} else if (inspection.models.empty()) {
			pendingStatus = "Endpoint reachable; enter a model ID (authentication not tested)";
		} else {
			pendingModelSelection = inspection.models.front();
			pendingStatus = "Endpoint reachable; model: " + pendingModelSelection +
				" (authentication not tested)";
		}
		if (inspection) ofLogNotice("ofxIC inspect") << pendingStatus;
		else ofLogError("ofxIC inspect") << pendingStatus;
		pendingOutput = currentOutput;
		finished = true;
	});
}

void ofApp::sendMessage() {
	if (!input[0] || mediaBusy || busy) return;
	if (selectedProfile == 0 && deferUntilRuntimeReady(DeferredTask::Chat)) return;
	if (busy.exchange(true)) return;
	activeTaskKind = "chat";
	const std::string message(input.data());
	input[0] = '\0';
	lastMessage = message;
	ofLogNotice("ofxIC chat") << "user: " << message;
	const auto currentOptions = chat.getOptions();
	ofLogNotice("ofxIC chat") << "route: endpoint=" << endpoint.getBaseUrl()
		<< " model=" << (currentOptions.model.empty() ? "<default>" : currentOptions.model);
	status = "Waiting for model...";
	{
		std::lock_guard<std::mutex> lock(resultMutex);
		pendingProgressStatus = streamChat
			? "Streaming direct chat..."
			: "Requesting model (request 1)...";
		pendingStreamOutput.clear();
	}
	cancellationRequested = false;
	requestCanCancel = true;
	focusMessageInput = true;
	const std::vector<std::string> currentModels = availableModels;
	const bool streaming = streamChat;
	const int profile = selectedProfile;
	worker = std::thread([this, message, currentModels, streaming, profile]() {
		ofxIC::RequestControl control;
		control.shouldCancel = [this]() { return cancellationRequested.load(); };
		ofxIC::ToolLoopResult result;
		if (streaming) {
			auto options = chat.getOptions();
			options.stream = true;
			chat.setOptions(options);
			const auto chatResult = chat.send(
				message,
				[this](const std::string & chunk) {
					ofLogNotice("ofxIC chat") << "assistant chunk: " << chunk;
					std::lock_guard<std::mutex> lock(resultMutex);
					pendingStreamOutput += chunk;
					return !cancellationRequested.load();
				},
				control);
			result.text = chatResult.text;
			result.error = chatResult.error;
			result.success = chatResult.success;
			result.cancelled = chatResult.cancelled;
			result.failure = chatResult.failure;
			result.modelRequests = 1;
		} else {
			auto options = chat.getOptions();
			options.stream = false;
			chat.setOptions(options);
			result = toolLoop.run(
				message,
				4,
				control,
				[this](const ofxIC::ToolLoopProgress & progress) {
					std::lock_guard<std::mutex> lock(resultMutex);
					if (progress.stage == ofxIC::ToolLoopStage::ExecutingTool) {
						pendingProgressStatus = "Executing allowlisted tool: " + progress.toolName;
					} else {
						pendingProgressStatus = "Requesting model (request " +
							ofToString(progress.modelRequest) + ")...";
					}
				});
		}
		if (result) {
			ofLogNotice("ofxIC chat") << "assistant: " << result.text;
		} else {
			ofLogError("ofxIC chat") << "request failed: " << result.error;
		}
		std::lock_guard<std::mutex> lock(resultMutex);
		pendingOutput = result.text;
		if (result) {
			pendingStatus = streaming ? "Streaming inference completed"
				: "Inference completed with " + ofToString(result.modelRequests) + " model request(s)";
		} else if (result.failure == ofxIC::RequestFailure::Cancelled) {
			pendingStatus = "Request cancelled";
		} else if (result.failure == ofxIC::RequestFailure::Timeout) {
			pendingStatus = "Request timed out";
		} else if (profile == 3 && result.error.find("HTTP 429") != std::string::npos &&
			result.error.find("quota") != std::string::npos) {
			pendingStatus = "OpenAI API quota exhausted. Add API billing/credits, then retry. "
				"ChatGPT subscriptions do not supply API quota.";
		} else {
			pendingStatus = "Request failed: " + result.error;
		}
		pendingModels = currentModels;
		finished = true;
	});
}

void ofApp::cancelRequest() {
	if ((!busy || !requestCanCancel) && !mediaBusy) return;
	cancellationRequested = true;
	if (mediaBusy) {
		mediaStatus = "Cancelling active media/music request...";
		musicStatus = "Cancelling active media/music request...";
	} else {
		status = "Cancelling request...";
	}
}

void ofApp::saveTokenCredential(
	const std::string & variable,
	std::array<char, 512> & input) {
	const std::string token(input.data());
	std::string error;
	if (!ofxICExample::saveCredential(variable, token, error)) {
		credentialStatus = "Could not save token: " + error;
		return;
	}
	storedTokens[variable] = token;
	input.fill('\0');
	endpoint.setBearerToken(configuredToken());
	transcriptionEndpoint.setBearerToken(configuredTranscriptionToken());
	segmentationEndpoint.setBearerToken(configuredSegmentationToken());
	mediaEndpoint.setBearerToken(configuredMediaToken());
	musicEndpoint.setBearerToken(configuredMusicToken());
	credentialStatus = "Saved " + variable + " in Windows Credential Manager.";
}

void ofApp::forgetTokenCredential(const std::string & variable) {
	std::string error;
	if (!ofxICExample::deleteCredential(variable, error)) {
		credentialStatus = "Could not forget token: " + error;
		return;
	}
	const auto stored = storedTokens.find(variable);
	if (stored != storedTokens.end()) {
		std::fill(stored->second.begin(), stored->second.end(), '\0');
		storedTokens.erase(stored);
	}
	endpoint.setBearerToken(configuredToken());
	transcriptionEndpoint.setBearerToken(configuredTranscriptionToken());
	segmentationEndpoint.setBearerToken(configuredSegmentationToken());
	mediaEndpoint.setBearerToken(configuredMediaToken());
	musicEndpoint.setBearerToken(configuredMusicToken());
	credentialStatus = "Removed saved " + variable + ". Environment overrides remain active.";
}

void ofApp::inspectMediaContext() {
	if (busy || mediaBusy.exchange(true)) return;
	pendingMediaCapabilitiesReady = false;
	cancellationRequested = false;
	activeMediaTaskKind = "media-context-inspection";
	mediaStatus = "Inspecting loaded sd-server context...";
	const std::string selectedContextModel = stableDiffusionModelPath.data();
	mediaWorker = std::thread([this, selectedContextModel]() {
		ofxIC::RequestControl control;
		control.timeoutSeconds = 10;
		control.shouldCancel = [this]() { return cancellationRequested.load(); };
		const auto capabilities = media.inspectCapabilities(control);
		std::string nextStatus;
		std::string nextOutput;
		if (!capabilities) {
			nextStatus = "Context inspection failed: " + capabilities.error;
		} else {
			const auto join = [](const std::vector<std::string> & values) {
				std::string result;
				for (const auto & value : values) {
					if (!result.empty()) result += ", ";
					result += value;
				}
				return result.empty() ? std::string("none") : result;
			};
			nextStatus = "Loaded context inspected successfully.";
			nextOutput = "Model: " + (capabilities.model.empty() ? "<unknown>" : capabilities.model) +
				"\nCurrent mode: " + (capabilities.currentMode.empty() ? "<unspecified>" : capabilities.currentMode) +
				"\nSupported modes: " + join(capabilities.supportedModes) +
				"\nImage formats: " + join(capabilities.imageOutputFormats) +
				"\nVideo formats: " + join(capabilities.videoOutputFormats) +
				"\nSize limits: " + std::to_string(capabilities.minWidth) + "-" +
					std::to_string(capabilities.maxWidth) + " x " +
					std::to_string(capabilities.minHeight) + "-" +
					std::to_string(capabilities.maxHeight) +
				"\nDefaults: " + std::to_string(capabilities.defaultWidth) + "x" +
					std::to_string(capabilities.defaultHeight) + ", " +
					std::to_string(capabilities.defaultVideoFrames) + " frames, " +
					std::to_string(capabilities.defaultFps) + " FPS, " +
					(capabilities.defaultOutputFormat.empty() ? "format unspecified" : capabilities.defaultOutputFormat) +
				"\nSamplers: " + join(capabilities.samplers) +
				"\nSchedulers: " + join(capabilities.schedulers);
			if (!ofxICExample::mediaModelMatches(capabilities.model, selectedContextModel)) {
				nextStatus = "Loaded context differs from the selected model.";
				nextOutput += "\nSelected model: " +
					ofFilePath::getFileName(selectedContextModel) +
					"\nRestart sd-server to load the selection.";
			}
		}
		std::lock_guard<std::mutex> lock(mediaResultMutex);
		pendingMediaStatus = std::move(nextStatus);
		pendingMediaOutput = std::move(nextOutput);
		pendingMediaSavedPath.clear();
		pendingMediaIsVideo = false;
		pendingMediaJob = {};
		pendingMediaCapabilities = capabilities;
		pendingMediaCapabilitiesReady = true;
		mediaFinished = true;
	});
}

void ofApp::generateMedia() {
	if (!mediaInput[0] || busy) return;
	if (!supportsMediaKind(selectedMediaBackend, selectedMediaKind)) {
		mediaStatus = unsupportedMediaMessage(selectedMediaBackend, selectedMediaKind);
		mediaOutput.clear();
		writeMediaAutomationResult(mediaStatus, mediaOutput);
		recordTaskHistory(selectedMediaKind == 1 ? "video-generation" : "image-generation",
			mediaStatus);
		return;
	}
	if (selectedMediaBackend == 2 && stableDiffusionProcess.running() &&
		!stableDiffusionActiveRuntimeSignature.empty() &&
		stableDiffusionActiveRuntimeSignature != stableDiffusionRuntimeSignature()) {
		if (!stableDiffusionProcess.ownsProcess()) {
			mediaStatus = "The external sd-server still owns its loaded model. Restart it with the selected checkpoint.";
			mediaOutput.clear();
			return;
		}
		mediaStatus = "Context configuration changed; restarting sd-server with " +
			ofFilePath::getFileName(stableDiffusionModelPath.data()) + "...";
		stopLocalStableDiffusionServer();
	}
	if (selectedMediaBackend == 2 && deferUntilRuntimeReady(DeferredTask::Media)) return;
	if (mediaBusy.exchange(true)) return;
	pendingMediaCapabilitiesReady = false;
	activeMediaTaskKind = selectedMediaKind == 1 ? "video-generation" : "image-generation";
	cancellationRequested = false;
	const std::string prompt(mediaInput.data());
	const int width = std::max(1, mediaWidth);
	const int height = std::max(1, mediaHeight);
	const int frames = std::max(1, mediaFrames);
	const int fps = std::max(1, mediaFps);
	const int seed = std::max(-1, mediaSeed);
	const int steps = std::max(1, mediaSteps);
	const float guidance = std::max(0.0f, mediaGuidance);
	const std::string sampler = mediaSampler;
	const std::string scheduler = mediaScheduler;
	const std::string requestedOutputFormat = mediaOutputFormat;
	const bool video = selectedMediaKind == 1;
	const int backend = selectedMediaBackend;
	const bool autoPoll = backend != 0;
	const std::string mediaModel = backend == 2
		? std::string(stableDiffusionModelPath.data())
		: (video ? std::string(mediaVideoModel.data()) : std::string(mediaImageModel.data()));
	const std::string outputStem = ofToDataPath(
		timestampedOutputFilename("media", ""), true);
	mediaStatus = backend == 1
		? (video ? "Submitting Hugging Face video..." : "Generating Hugging Face image...")
		: (backend == 2 ? "Submitting native media job..." : "Generating OpenAI image...");
	mediaWorker = std::thread([this, prompt, width, height, frames, fps, seed, steps,
		guidance, sampler, scheduler, requestedOutputFormat, video, backend,
		mediaModel, autoPoll, outputStem]() {
		ofxIC::RequestControl control;
		control.shouldCancel = [this]() { return cancellationRequested.load(); };
		std::string nextStatus;
		std::string nextOutput;
		std::string nextBase64;
		std::string nextBytes;
		std::string nextFormat;
		std::string nextSavedPath;
		ofxIC::MediaJob nextJob;
		if (backend == 0) {
			ofxIC::ImageRequest request;
			request.prompt = prompt;
			request.model = mediaModel;
			request.width = width;
			request.height = height;
			const auto result = media.generateImage(request, control);
			nextStatus = result
				? "OpenAI image generation completed"
				: "OpenAI image generation failed: " + result.error;
			if (!result.imagesBase64.empty()) {
				nextBase64 = result.imagesBase64.front();
				nextFormat = result.outputFormat.empty() ? "png" : result.outputFormat;
				nextOutput = "Received " + ofToString(result.imagesBase64.size()) + " image payload(s)";
			} else if (!result.urls.empty()) {
				nextOutput = result.urls.front();
				const auto downloaded = media.downloadImage(result.urls.front(), control);
				if (downloaded) {
					nextBytes = downloaded.imageBytes;
					nextFormat = downloaded.outputFormat;
				} else nextStatus = "Image generated, but download failed: " + downloaded.error;
			}
		} else {
			ofxIC::MediaJobRequest request;
			request.kind = video ? ofxIC::MediaKind::Video : ofxIC::MediaKind::Image;
			request.prompt = prompt;
			request.model = mediaModel;
			request.width = width;
			request.height = height;
			request.videoFrames = frames;
			request.fps = fps;
			if (backend == 2) {
				request.seed = seed;
				request.steps = steps;
				request.guidance = guidance;
				request.sampleMethod = sampler;
				request.scheduler = scheduler;
				request.outputFormat = requestedOutputFormat;
			}
			nextJob = backend == 1
				? media.submitHuggingFaceFal(request, control)
				: media.submit(request, control);
			const auto pollingDeadline = std::chrono::steady_clock::now() + std::chrono::minutes(30);
			control.shouldCancel = [this, pollingDeadline]() {
				return cancellationRequested.load() || std::chrono::steady_clock::now() >= pollingDeadline;
			};
			while (autoPoll && nextJob && !nextJob.terminal() && !control.shouldCancel()) {
				{
					std::lock_guard<std::mutex> lock(mediaResultMutex);
					pendingMediaProgressStatus = std::string(video ? "Video" : "Image") +
						" job " + nextJob.id + " is " + mediaJobStateLabel(nextJob.state) +
						"; waiting automatically...";
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
					pollingDeadline - std::chrono::steady_clock::now()).count();
				if (remaining <= 0 || control.shouldCancel()) break;
				control.timeoutSeconds = static_cast<int>(std::min<long long>(30, remaining));
				nextJob = media.poll(nextJob, control);
			}
			if (autoPoll && ((nextJob && !nextJob.terminal()) ||
				(std::chrono::steady_clock::now() >= pollingDeadline && nextJob.state != ofxIC::MediaJobState::Completed))) {
				nextJob.success = false;
				nextJob.cancelled = cancellationRequested.load();
				nextJob.failure = nextJob.cancelled ? ofxIC::RequestFailure::Cancelled : ofxIC::RequestFailure::Timeout;
				nextJob.state = nextJob.cancelled ? ofxIC::MediaJobState::Cancelled : ofxIC::MediaJobState::Failed;
				nextJob.error = nextJob.cancelled ? "media job polling cancelled"
					: "media job timed out after 30 minutes of automatic polling";
			}
			nextStatus = nextJob
				? std::string(video ? "Video" : "Image") + " job " + nextJob.id +
					" is " + mediaJobStateLabel(nextJob.state)
				: std::string(video ? "Video" : "Image") + " request failed: " + nextJob.error;
			nextOutput = nextJob.pollUrl;
			nextFormat = nextJob.outputFormat;
			if (video && nextJob.frameCount > 0) {
				nextOutput += (nextOutput.empty() ? "" : "\n") +
					std::string("Video metadata: ") + ofToString(nextJob.frameCount) +
					" frames" + (nextJob.fps > 0 ? " at " + ofToString(nextJob.fps) + " FPS" : "");
			}
			if (!nextJob.payloadBytes.empty()) {
				nextBytes = nextJob.payloadBytes.front();
				nextOutput = "Received " + ofToString(nextBytes.size()) + " media bytes";
			} else if (!nextJob.payloadsBase64.empty()) {
				nextBase64 = nextJob.payloadsBase64.front();
			}
		}
		if (nextBytes.empty() && !nextBase64.empty()) nextBytes = decodeBase64(nextBase64);
		if (!nextBytes.empty()) {
			const std::string extension = nextFormat.empty()
				? (video ? "webm" : "png") : nextFormat;
			const std::string path = outputStem + "." + extension;
			if (ofBufferToFile(path, ofBuffer(nextBytes.data(), nextBytes.size()))) {
				nextSavedPath = path;
				nextOutput += "\nSaved: " + path;
			} else {
				nextOutput += "\nCould not save generated media.";
			}
		}
		std::lock_guard<std::mutex> lock(mediaResultMutex);
		pendingMediaStatus = std::move(nextStatus);
		pendingMediaOutput = std::move(nextOutput);
		pendingMediaSavedPath = std::move(nextSavedPath);
		pendingMediaIsVideo = video;
		pendingMediaJob = std::move(nextJob);
		mediaFinished = true;
	});
}

void ofApp::generateMusic() {
	if (!musicInput[0] || busy || mediaBusy) return;
	if (selectedMusicBackend == 0 && deferUntilRuntimeReady(DeferredTask::Music)) return;
	if (mediaBusy.exchange(true)) return;
	activeMusicTaskKind = "music-generation";
	const std::string prompt(musicInput.data());
	cancellationRequested = false;
	const int duration = musicDuration;
	const std::string format = musicOutputFormat == 1 ? "wav" : "mp3";
	const int backend = selectedMusicBackend;
	const bool autoPoll = true;
	musicStatus = backend == 0
		? "Submitting local ACE-Step music job..."
		: "Submitting Stability Audio 3 music job...";
	musicOutput.clear();
	mediaWorker = std::thread([this, prompt, duration, format, backend, autoPoll]() {
		ofxIC::RequestControl control;
		control.shouldCancel = [this]() { return cancellationRequested.load(); };
		std::string nextStatus;
		std::string nextOutput;
		std::string nextBytes;
		ofxIC::StabilityAudioJob nextStabilityJob;
		ofxIC::AceStepMusicJob nextAceStepJob;
		if (backend == 0) {
			ofxIC::AceStepMusicRequest request;
			request.caption = prompt;
			request.durationSeconds = duration;
			request.outputFormat = format;
			request.protocol = isNativeAceStepServer(aceStepServerPath.data())
				? ofxIC::AceStepMusicProtocol::NativeCpp
				: ofxIC::AceStepMusicProtocol::Official15;
			if (request.protocol == ofxIC::AceStepMusicProtocol::NativeCpp)
				request.nativeSynthModel = preferredNativeAceStepSynthModel(
					aceStepModelDirectory.data());
			nextAceStepJob = aceStepMusic.submit(request, control);
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(20);
			while (autoPoll && nextAceStepJob && !nextAceStepJob.terminal() &&
				!cancellationRequested && std::chrono::steady_clock::now() < deadline) {
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				nextAceStepJob = aceStepMusic.poll(nextAceStepJob, control);
			}
			if (autoPoll && nextAceStepJob && !nextAceStepJob.terminal()) {
				nextAceStepJob.success = false;
				nextAceStepJob.state = ofxIC::AceStepMusicJobState::Failed;
				nextAceStepJob.error = cancellationRequested
					? "music automation cancelled"
					: "music automation timed out while polling";
			}
			nextStatus = nextAceStepJob
				? (nextAceStepJob.id.empty() ? "Local music generation completed"
					: "ACE-Step job " + nextAceStepJob.id + " is " +
						musicJobStateLabel(nextAceStepJob.state))
				: "Local music request failed: " + nextAceStepJob.error;
			if (nextAceStepJob.state == ofxIC::AceStepMusicJobState::Completed) {
				nextBytes = nextAceStepJob.audioBytes;
				nextOutput = "Received " + ofToString(nextBytes.size()) + " local audio bytes";
			} else if (nextAceStepJob) {
				nextOutput = nextAceStepJob.error.empty()
					? "Local music generation did not complete." : nextAceStepJob.error;
			}
		} else {
			ofxIC::StabilityAudioRequest request;
			request.prompt = prompt;
			request.durationSeconds = duration;
			request.outputFormat = format;
			nextStabilityJob = stabilityMusic.submit(request, control);
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(20);
			while (autoPoll && nextStabilityJob && !nextStabilityJob.terminal() &&
				!cancellationRequested && std::chrono::steady_clock::now() < deadline) {
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				nextStabilityJob = stabilityMusic.poll(nextStabilityJob, control);
			}
			if (autoPoll && nextStabilityJob && !nextStabilityJob.terminal()) {
				nextStabilityJob.success = false;
				nextStabilityJob.state = ofxIC::StabilityAudioJobState::Failed;
				nextStabilityJob.error = cancellationRequested
					? "music automation cancelled"
					: "music automation timed out while polling";
			}
			nextStatus = nextStabilityJob
				? "Music job " + nextStabilityJob.id + " is " +
					musicJobStateLabel(nextStabilityJob.state)
				: "Music request failed: " + nextStabilityJob.error;
			if (nextStabilityJob.state == ofxIC::StabilityAudioJobState::Completed) {
				nextBytes = nextStabilityJob.audioBytes;
				nextOutput = "Received " + ofToString(nextBytes.size()) + " audio bytes";
			} else if (nextStabilityJob) {
				nextOutput = nextStabilityJob.error.empty()
					? "Music generation did not complete." : nextStabilityJob.error;
			}
		}
		std::lock_guard<std::mutex> lock(mediaResultMutex);
		pendingMusicStatus = std::move(nextStatus);
		pendingMusicOutput = std::move(nextOutput);
		pendingMusicBytes = std::move(nextBytes);
		pendingMusicFormat = format;
		pendingMusicJob = std::move(nextStabilityJob);
		pendingAceStepMusicJob = std::move(nextAceStepJob);
		musicFinished = true;
	});
}

void ofApp::finishWorker() {
	if (worker.joinable()) worker.join();
}

void ofApp::finishMediaWorker() {
	if (mediaWorker.joinable()) mediaWorker.join();
}
