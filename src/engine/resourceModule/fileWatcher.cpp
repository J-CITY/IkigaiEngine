#include "fileWatcher.h"

#include "fileSystem/fileSystem.h"
#include "serviceManager.h"
#include "utilsModule/exeptions.h"
#include <iostream>

namespace IKIGAI::RESOURCES {

#ifndef USE_FILE_WATCHER

IKIGAI::IdGenerator<IKIGAI::EVENT::Event<>>::ID FileWatcher::_add(const std::string&, std::function<void(FileStatus)>) {
	return EVENT::Event<FileStatus>::ID(0);
}

IKIGAI::IdGenerator<IKIGAI::EVENT::Event<>>::ID FileWatcher::add(const Path&, std::function<void(FileStatus)>) {
	return EVENT::Event<FileStatus>::ID(0);
}

void FileWatcher::addDeferred(const Path&, std::function<void(FileStatus)>, std::function<void(EVENT::Event<FileStatus>::ID)>) { }

void FileWatcher::_remove(const std::string&, EVENT::Event<FileStatus>::ID) { }

void FileWatcher::remove(const Path&, EVENT::Event<FileStatus>::ID) { }

void FileWatcher::removeDeferred(const Path&, EVENT::Event<FileStatus>::ID) { }

void FileWatcher::start() { }

void FileWatcher::stop() { }

void FileWatcher::workerLoop() { }

void FileWatcher::applyUpdate() { }

void FileWatcher::update() { }
void FileWatcher::requestUpdate() { }

#else

IKIGAI::IdGenerator<IKIGAI::EVENT::Event<>>::ID FileWatcher::_add(const std::string& path, std::function<void(FileStatus)> cb) {
	auto& fs = ServiceManager::Get<FileSystem>();
	if (!fs.isFileExist(path)) {
		throw UTILS::EXEPTIONS::WrongPath(path.c_str());
	}
	m_filesLastModifications[path].mLastModifyTime = fs.lastWriteTime(path);
	m_filesLastModifications[path].mFileSize = fs.fileSize(path);
	m_filesLastModifications[path].mNativePath = fs.getAbsolutePath(path);
	if (!m_filesCallbacks.contains(path)) {
		m_filesCallbacks[path] = EVENT::Event<FileStatus>();
	}
	return m_filesCallbacks[path].add(cb);
};

IKIGAI::IdGenerator<IKIGAI::EVENT::Event<>>::ID FileWatcher::add(const Path& path, std::function<void(FileStatus)> cb) {
	const std::lock_guard lock(m_mutex);
	const auto _path = path.string();
	return _add(_path, cb);
}

void FileWatcher::addDeferred(const Path& path, std::function<void(FileStatus)> cb, std::function<void(EVENT::Event<FileStatus>::ID)> retCb) {
	const std::lock_guard lock(m_mutexDeferred);
	const auto _path = path.string();
	if (!ServiceManager::Get<FileSystem>().isFileExist(_path)) {
		throw UTILS::EXEPTIONS::WrongPath(_path.c_str());
	}

	deferredEvents.push({ QueueEvent::Action::ADD, _path, cb, retCb });
}

void FileWatcher::_remove(const std::string& path, EVENT::Event<FileStatus>::ID id) {
	if (!m_filesCallbacks.contains(path)) {
		return;
	}
	m_filesCallbacks.at(path).removeListener(id);
	if (!m_filesCallbacks.at(path).getListenerCount()) {
		m_filesCallbacks.erase(path);
		m_filesLastModifications.erase(path);
	}
}

void FileWatcher::remove(const Path& path, EVENT::Event<FileStatus>::ID id) {
	const std::lock_guard lock(m_mutex);
	const auto _path = path.string();
	_remove(_path, id);
}

void FileWatcher::removeDeferred(const Path& path, EVENT::Event<FileStatus>::ID id) {
	const std::lock_guard lock(m_mutexDeferred);
	const auto _path = path.string();
	deferredEvents.push({ QueueEvent::Action::REMOVE, _path, nullptr, nullptr, id });
}

void FileWatcher::start() {
	const std::lock_guard lock(mWorkerMutex);
	mWorkerStop = false;
	m_isRunning = true;
}

void FileWatcher::stop() {
	std::thread worker;
	{
		const std::lock_guard lock(mWorkerMutex);
		m_isRunning = false;
		mWorkerStop = true;
		mUpdateRequested = false;
		worker = std::move(mWorker);
	}
	mWorkerCv.notify_one();
	if (worker.joinable()) {
		worker.join();
	}
}

void FileWatcher::requestUpdate() {
	{
		const std::lock_guard lock(mWorkerMutex);
		if (!m_isRunning || mWorkerStop) {
			return;
		}
		mUpdateRequested = true;
		if (!mWorker.joinable()) {
			mWorker = std::thread([this] { workerLoop(); });
		}
	}
	mWorkerCv.notify_one();
}

void FileWatcher::workerLoop() {
	while (true) {
		{
			std::unique_lock lock(mWorkerMutex);
			mWorkerCv.wait(lock, [this] { return mWorkerStop || mUpdateRequested; });
			if (mWorkerStop) {
				return;
			}
			mUpdateRequested = false;
		}
		try {
			update();
		} catch (const std::exception& error) {
			std::cerr << "File watcher: " << error.what() << '\n';
		} catch (...) {
			std::cerr << "File watcher: unknown error\n";
		}
	}
}

void FileWatcher::applyUpdate() {
	decltype(m_events) events;
	{
		const std::lock_guard lock(m_mutex);
		events.swap(m_events);
	}
	for (const auto& [path, status] : events) {
		std::optional<EVENT::Event<FileStatus>> callbacks;
		{
			const std::lock_guard lock(m_mutex);
			if (const auto found = m_filesCallbacks.find(path); found != m_filesCallbacks.end()) {
				callbacks.emplace(found->second);
			}
		}
		// Reload callbacks may remove and re-register their own watches.
		if (callbacks) {
			callbacks->run(status);
		}
	}
	decltype(deferredEvents) pending;
	{
		const std::lock_guard lock(m_mutexDeferred);
		pending.swap(deferredEvents);
	}
	while (!pending.empty()) {
		auto event = std::move(pending.front());
		pending.pop();
		if (event.action == QueueEvent::Action::ADD) {
			EVENT::Event<FileStatus>::ID listener {};
			{
				const std::lock_guard stateLock(m_mutex);
				listener = _add(event.path, std::move(event.cb));
			}
			event.retCb(listener);
		}
		else {
			const std::lock_guard stateLock(m_mutex);
			_remove(event.path, event.id.value());
		}
	}
}

void FileWatcher::update() {
	if (!m_isRunning) {
		return;
	}
	decltype(m_filesLastModifications) snapshot;
	{
		const std::lock_guard lock(m_mutex);
		snapshot = m_filesLastModifications;
	}
	for (const auto& [path, previous] : snapshot) {
		if (!m_isRunning) {
			break;
		}
		// Packed assets are immutable. Cache native paths at registration so the
		// worker performs only filesystem metadata queries, without touching VFS.
		if (!previous.mNativePath) {
			continue;
		}
		std::error_code error;
		const auto native = std::filesystem::u8path(*previous.mNativePath);
		const bool exists = std::filesystem::exists(native, error);
		if (error) {
			continue;
		}
		const auto modified = exists ? std::filesystem::last_write_time(native, error) : FileTime {};
		if (error) {
			continue;
		}
		const auto size = exists ? std::filesystem::file_size(native, error) : uintmax_t {};
		if (error || (exists && modified == previous.mLastModifyTime && size == previous.mFileSize)) {
			continue;
		}
		const std::lock_guard lock(m_mutex);
		const auto current = m_filesLastModifications.find(path);
		if (current == m_filesLastModifications.end() || current->second.mNativePath != previous.mNativePath
			|| current->second.mLastModifyTime != previous.mLastModifyTime || current->second.mFileSize != previous.mFileSize) {
			continue;
		}
		current->second.mLastModifyTime = modified;
		current->second.mFileSize = size;
		m_events.emplace_back(path, exists ? FileStatus::MODIFIED : FileStatus::DEL);
	}
}

#endif

}
