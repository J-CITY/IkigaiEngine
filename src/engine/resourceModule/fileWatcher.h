#pragma once

#if defined(USE_FILE_WATCHER) && !defined(USE_EDITOR)
#error "USE_FILE_WATCHER requires USE_EDITOR"
#endif

#include <filesystem>
#include <functional>
#include <string>
#include <atomic>
#include <queue>
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
			std::uintmax_t mFileSize{};
		};

		std::unordered_map<std::string, FileModificationInfo> m_filesLastModifications;
		std::unordered_map<std::string, EVENT::Event<FileStatus>> m_filesCallbacks;
		std::vector<std::pair<std::string, FileStatus>> m_events;

		std::atomic_bool m_isRunning = true;
		std::mutex m_mutex;
		std::mutex m_mutexDeferred;

		struct QueueEvent {
			enum class Action {ADD, REMOVE};
			Action action;
			std::string path;
			std::function<void(FileStatus)> cb;
			std::function<void(EVENT::Event<FileStatus>::ID)> retCb;
			std::optional<EVENT::Event<FileStatus>::ID> id;
		};
		std::queue<QueueEvent> deferredEvents;

		std::chrono::duration<int, std::milli> m_delay = std::chrono::milliseconds(DELAY_UPDATE);

		EVENT::Event<FileStatus>::ID _add(const std::string& path, std::function<void(FileStatus)> cb);
		void _remove(const std::string& path, EVENT::Event<FileStatus>::ID id);

	public:
		inline static FileWatcher* m_instance = nullptr;
		static FileWatcher* getInstance() {
			if (!m_instance) {
				m_instance = new FileWatcher();
			}
			return m_instance;
		}

		EVENT::Event<FileStatus>::ID add(const Path& path, std::function<void(FileStatus)> cb);
		void remove(const Path& path, EVENT::Event<FileStatus>::ID id);

		void addDeferred(const Path& path, std::function<void(FileStatus)> cb, std::function<void(EVENT::Event<FileStatus>::ID)> retCb);
		void removeDeferred(const Path& path, EVENT::Event<FileStatus>::ID id);

		void start();
		void stop();

		void applyUpdate();
		void update();
	};
}
