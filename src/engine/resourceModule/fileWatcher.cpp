#include "fileWatcher.h"

#include "fileSystem/fileSystem.h"
#include "serviceManager.h"
#include "utilsModule/exeptions.h"
#include <iostream>
#include <unordered_set>

namespace IKIGAI::RESOURCES {

#ifndef USE_FILE_WATCHER

IKIGAI::IdGenerator<IKIGAI::EVENT::Event<>>::ID FileWatcher::_add(const std::string&, Listener) {
	return {};
}

IKIGAI::IdGenerator<IKIGAI::EVENT::Event<>>::ID FileWatcher::add(const Path&, std::function<void(FileStatus)>) {
	return {};
}

EVENT::Event<FileWatcher::FileStatus>::ID FileWatcher::addResourceWatch(
	const Path&, std::function<void(FileStatus)>, std::shared_ptr<const void>, std::weak_ptr<void>) {
	return {};
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

IKIGAI::IdGenerator<IKIGAI::EVENT::Event<>>::ID FileWatcher::_add(const std::string& path, Listener listener) {
	auto& fs = ServiceManager::Get<FileSystem>();
	const bool exists = fs.isFileExist(path);
	if (!exists && !listener.group) {
		throw UTILS::EXEPTIONS::WrongPath(path.c_str());
	}
	auto native = listener.group ? fs.getNativeWatchPath(path) : fs.getAbsolutePath(path);
	FileModificationInfo info { exists ? fs.lastWriteTime(path) : FileTime {}, exists ? fs.fileSize(path) : 0, std::move(native), 0, exists };
	if (listener.group && !info.mNativePath) {
		return {};
	}
	const std::lock_guard lock(m_mutex);
	const auto current = m_filesLastModifications.find(path);
	if (current == m_filesLastModifications.end() || current->second.mNativePath != info.mNativePath) {
		info.mRegistration = ++mNextRegistration;
		m_filesLastModifications.insert_or_assign(path, std::move(info));
	}
	const auto id = EVENT::Event<FileStatus>::generateId();
	m_filesCallbacks[path].emplace(id, std::move(listener));
	return id;
}

IKIGAI::IdGenerator<IKIGAI::EVENT::Event<>>::ID FileWatcher::add(const Path& path, std::function<void(FileStatus)> cb) {
	return _add(FileSystem::normalizePath(path.string()), { std::move(cb), {}, {} });
}

EVENT::Event<FileWatcher::FileStatus>::ID FileWatcher::addResourceWatch(
	const Path& path, std::function<void(FileStatus)> cb, std::shared_ptr<const void> group, std::weak_ptr<void> resource) {
	return _add(FileSystem::normalizePath(path.string()), { std::move(cb), std::move(group), std::move(resource) });
}

void FileWatcher::addDeferred(const Path& path, std::function<void(FileStatus)> cb, std::function<void(EVENT::Event<FileStatus>::ID)> retCb) {
	const auto key = FileSystem::normalizePath(path.string());
	if (!ServiceManager::Get<FileSystem>().isFileExist(key)) {
		throw UTILS::EXEPTIONS::WrongPath(key.c_str());
	}
	const std::lock_guard lock(m_mutexDeferred);
	deferredEvents.push({ QueueEvent::Action::ADD, key, std::move(cb), std::move(retCb) });
}

void FileWatcher::_remove(const std::string& path, EVENT::Event<FileStatus>::ID id) {
	if (!m_filesCallbacks.contains(path)) {
		return;
	}
	m_filesCallbacks.at(path).erase(id);
	if (m_filesCallbacks.at(path).empty()) {
		m_filesCallbacks.erase(path);
		m_filesLastModifications.erase(path);
		m_events.erase(path);
	}
}

void FileWatcher::remove(const Path& path, EVENT::Event<FileStatus>::ID id) {
	const std::lock_guard lock(m_mutex);
	_remove(FileSystem::normalizePath(path.string()), id);
}

void FileWatcher::removeDeferred(const Path& path, EVENT::Event<FileStatus>::ID id) {
	const std::lock_guard lock(m_mutexDeferred);
	const auto _path = FileSystem::normalizePath(path.string());
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
		for (auto it = m_filesCallbacks.begin(); it != m_filesCallbacks.end();) {
			std::erase_if(it->second, [](const auto& item) {
				const auto& listener = item.second;
				return listener.group && listener.resource.expired();
			});
			if (it->second.empty()) {
				m_filesLastModifications.erase(it->first);
				it = m_filesCallbacks.erase(it);
			}
			else {
				++it;
			}
		}
	}
	std::unordered_set<std::shared_ptr<const void>> dispatched;
	for (const auto& [path, change] : events) {
		std::map<EVENT::Event<FileStatus>::ID, Listener> callbacks;
		{
			const std::lock_guard lock(m_mutex);
			if (const auto info = m_filesLastModifications.find(path);
				info != m_filesLastModifications.end() && info->second.mRegistration == change.registration) {
				callbacks = m_filesCallbacks.at(path);
			}
		}
		for (const auto& [id, listener] : callbacks) {
			if (listener.group && (change.status == FileStatus::DEL || listener.resource.expired() || !dispatched.insert(listener.group).second)) {
				continue;
			}
			try {
				listener.callback(change.status);
			} catch (const std::exception& error) {
				std::cerr << "File watcher listener " << path << ": " << error.what() << '\n';
			} catch (...) {
				std::cerr << "File watcher listener " << path << ": unknown error\n";
			}
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
		try {
			if (event.action == QueueEvent::Action::ADD) {
				const auto listener = _add(event.path, { std::move(event.cb), {}, {} });
				try {
					event.retCb(listener);
				} catch (...) {
					remove(event.path, listener);
					throw;
				}
			}
			else {
				remove(event.path, event.id.value());
			}
		} catch (const std::exception& error) {
			std::cerr << "File watcher subscription " << event.path << ": " << error.what() << '\n';
		} catch (...) {
			std::cerr << "File watcher subscription " << event.path << ": unknown error\n";
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
		if (error || (exists == previous.mExists && modified == previous.mLastModifyTime && size == previous.mFileSize)) {
			continue;
		}
		const std::lock_guard lock(m_mutex);
		const auto current = m_filesLastModifications.find(path);
		if (current == m_filesLastModifications.end() || current->second.mRegistration != previous.mRegistration
			|| current->second.mLastModifyTime != previous.mLastModifyTime || current->second.mFileSize != previous.mFileSize
			|| current->second.mExists != previous.mExists) {
			continue;
		}
		current->second.mLastModifyTime = modified;
		current->second.mFileSize = size;
		current->second.mExists = exists;
		m_events.insert_or_assign(path, PendingChange { exists ? FileStatus::MODIFIED : FileStatus::DEL, previous.mRegistration });
	}
}

#endif

}
