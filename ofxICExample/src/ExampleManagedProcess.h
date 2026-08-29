#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ofxICExample {

enum class ManagedProcessState {
	Stopped,
	Starting,
	Ready,
	Exited,
	Failed,
};

const char * managedProcessStateLabel(ManagedProcessState state);

class ManagedProcess {
public:
	ManagedProcess();
	~ManagedProcess();
	ManagedProcess(const ManagedProcess &) = delete;
	ManagedProcess & operator=(const ManagedProcess &) = delete;

	bool start(
		const std::string & executable,
		const std::vector<std::string> & arguments,
		const std::string & name,
		unsigned short readinessPort,
		const std::string & workingDirectory = {});
	bool useExisting(const std::string & name, unsigned short readinessPort);
	void update();
	void stop();

	bool running() const;
	bool ownsProcess() const;
	ManagedProcessState state() const;
	unsigned long processId() const;
	int exitCode() const;
	const std::string & status() const;
	const std::string & recentOutput() const;
	std::string takeNewOutput();

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

} // namespace ofxICExample
