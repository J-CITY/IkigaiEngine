#pragma once

#if defined(USE_FILE_WATCHER) && !defined(USE_EDITOR)
#error "USE_FILE_WATCHER requires USE_EDITOR"
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <utilsModule/event.h>

namespace IKIGAI::RESOURCES {
class FileWatcher {
public:
	constexpr static int DELAY_UPDATE = 5000;
	enum class FileStatus { CREATE, MODIFIED, DEL };

private:
	using FileTime = std::filesystem::file_time_type;
	using Path = std::filesystem::path;

	struct FileModificationInfo {
		FileTime mLastModifyTime;
		std::uintmax_t mFileSize {};
		std::optional<std::string> mNativePath;
		uint64_t mRegistration = 0;
		bool mExists = false;
	};

	struct Listener {
		std::function<void(FileStatus)> callback;
		std::shared_ptr<const void> group;
		std::weak_ptr<void> resource;
	};

	struct PendingChange {
		FileStatus status;
		uint64_t registration;
	};

	std::unordered_map<std::string, FileModificationInfo> m_filesLastModifications;
	std::unordered_map<std::string, std::map<EVENT::Event<FileStatus>::ID, Listener>> m_filesCallbacks;
	std::unordered_map<std::string, PendingChange> m_events;
	uint64_t mNextRegistration = 0;

	std::atomic_bool m_isRunning = true;
	std::mutex m_mutex;
	std::mutex m_mutexDeferred;
	void workerLoop();
	std::thread mWorker;
	std::mutex mWorkerMutex;
	std::condition_variable mWorkerCv;
	bool mUpdateRequested = false;
	bool mWorkerStop = false;

	struct QueueEvent {
		enum class Action { ADD, REMOVE };
		Action action;
		std::string path;
		std::function<void(FileStatus)> cb;
		std::function<void(EVENT::Event<FileStatus>::ID)> retCb;
		std::optional<EVENT::Event<FileStatus>::ID> id;
	};
	std::queue<QueueEvent> deferredEvents;

	std::chrono::duration<int, std::milli> m_delay = std::chrono::milliseconds(DELAY_UPDATE);

	EVENT::Event<FileStatus>::ID _add(const std::string& path, Listener listener);
	void _remove(const std::string& path, EVENT::Event<FileStatus>::ID id);

public:
	~FileWatcher() {
		stop();
	}
	static FileWatcher* getInstance() {
		static auto* instance = new FileWatcher();
		return instance;
	}

	EVENT::Event<FileStatus>::ID add(const Path& path, std::function<void(FileStatus)> cb);
	EVENT::Event<FileStatus>::ID addResourceWatch(
		const Path& path, std::function<void(FileStatus)> cb, std::shared_ptr<const void> group, std::weak_ptr<void> resource);
	void remove(const Path& path, EVENT::Event<FileStatus>::ID id);

	void addDeferred(const Path& path, std::function<void(FileStatus)> cb, std::function<void(EVENT::Event<FileStatus>::ID)> retCb);
	void removeDeferred(const Path& path, EVENT::Event<FileStatus>::ID id);

	void start();
	void stop();

	// Run callbacks on the application thread, before simulation and render recording.
	void applyUpdate();
	void requestUpdate();
	void update();
};
}
