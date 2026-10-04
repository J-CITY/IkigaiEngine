#include "fileWatcher.h"

#include "serviceManager.h"
#include "fileSystem/fileSystem.h"
#include "utilsModule/exeptions.h"

namespace IKIGAI::RESOURCES {

#ifndef USE_FILE_WATCHER

IKIGAI::IdGenerator<IKIGAI::EVENT::Event<>>::ID FileWatcher::_add(const std::string&, std::function<void(FileStatus)>) {
	return EVENT::Event<FileStatus>::ID(0);
}

IKIGAI::IdGenerator<IKIGAI::EVENT::Event<>>::ID FileWatcher::add(const Path&, std::function<void(FileStatus)>) {
	return EVENT::Event<FileStatus>::ID(0);
}

void FileWatcher::addDeferred(const Path&, std::function<void(FileStatus)>, std::function<void(EVENT::Event<FileStatus>::ID)>) {}

void FileWatcher::_remove(const std::string&, EVENT::Event<FileStatus>::ID) {}

void FileWatcher::remove(const Path&, EVENT::Event<FileStatus>::ID) {}

void FileWatcher::removeDeferred(const Path&, EVENT::Event<FileStatus>::ID) {}

void FileWatcher::start() {}

void FileWatcher::stop() {}

void FileWatcher::applyUpdate() {}

void FileWatcher::update() {}

#else

IKIGAI::IdGenerator<IKIGAI::EVENT::Event<>>::ID FileWatcher::_add(const std::string& path, std::function<void(FileStatus)> cb) {
	auto& fs = ServiceManager::Get<FileSystem>();
	if (!fs.isFileExist(path)) {
		throw UTILS::EXEPTIONS::WrongPath(path.c_str());
	}
	m_filesLastModifications[path].mLastModifyTime = fs.lastWriteTime(path);
	m_filesLastModifications[path].mFileSize = fs.fileSize(path);
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
	if (!ServiceManager::Get<FileSystem>().isFileExist(path.string())) {
		throw;
	}
	const auto _path = path.string();

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
	auto& fs = ServiceManager::Get<FileSystem>();
	const auto _path = path.string();
	if (!fs.isFileExist(_path)) {
		throw UTILS::EXEPTIONS::WrongPath(_path.c_str());;
	}
	deferredEvents.push({ QueueEvent::Action::REMOVE, _path, nullptr, nullptr, id });
}

void FileWatcher::start() {
	m_isRunning = true;
}

void FileWatcher::stop() {
	m_isRunning = false;
}

void FileWatcher::applyUpdate() {
	{
		const std::lock_guard lock(m_mutex);
		for (auto& e : m_events) {
			m_filesCallbacks[e.first].run(e.second);
		}
		m_events.clear();
	}
	{
		const std::lock_guard lock(m_mutexDeferred);
		while (!deferredEvents.empty()) {
			auto e = deferredEvents.front();
			deferredEvents.pop();

			if (e.action == QueueEvent::Action::ADD) {
				e.retCb(_add(e.path, e.cb));
			}
			else {
				_remove(e.path, e.id.value());
			}
		}
	}

}

void FileWatcher::update() {
	if (!m_isRunning) {
		return;
	}
	const std::lock_guard lockm(m_mutex);
	for (auto& file : m_filesLastModifications) {
		if (!ServiceManager::Get<FileSystem>().isFileExist(file.first)) {
			m_events.emplace_back(file.first, FileStatus::DEL);
		}
		else {
			auto& fs = ServiceManager::Get<FileSystem>();
			auto lastModification = fs.lastWriteTime(file.first);
			auto fileSize = fs.fileSize(file.first);

			if (lastModification != file.second.mLastModifyTime || fileSize != file.second.mFileSize) {
				m_filesLastModifications[file.first] = {lastModification, fileSize};
				m_events.emplace_back(file.first, FileStatus::MODIFIED);
			}
		}
	}
}

#endif

}
