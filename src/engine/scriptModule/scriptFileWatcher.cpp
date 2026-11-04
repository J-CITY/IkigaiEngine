#include "scriptFileWatcher.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include <algorithm>

#if defined(USE_FILE_WATCHER)
namespace IKIGAI::SCRIPTING {
ScriptFileWatcher& ScriptFileWatcher::instance() {
	static ScriptFileWatcher hub;
	return hub;
}

ScriptFileWatcher::Handle ScriptFileWatcher::watch(const std::string& vfsPath, ReloadFn onModified) {
	auto& fs = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
	const auto native = fs.getAbsolutePath(vfsPath);
	if (!native || native->empty() || !onModified) {
		return 0;
	}

	auto& hub = instance();
	std::lock_guard lock(hub.mMutex);
	const Handle handle = ++hub.mNextHandle;
	auto [slotIt, inserted] = hub.mNativeSlots.try_emplace(*native);
	auto& slot = slotIt->second;
	try {
		hub.mCallbacks.emplace(handle, CallbackEntry { std::move(onModified) });
		slot.handles.push_back(handle);
		if (!slot.watchId.valid()) {
			slot.vfsPath = vfsPath;
			// FileWatcher resolves native metadata itself and accepts only VFS paths.
			slot.watchId = RESOURCES::FileWatcher::getInstance()->add(vfsPath, [nativePath = *native](RESOURCES::FileWatcher::FileStatus status) {
				if (status == RESOURCES::FileWatcher::FileStatus::MODIFIED) {
					instance().dispatchNative(nativePath);
				}
			});
		}
	} catch (...) {
		hub.mCallbacks.erase(handle);
		slot.handles.erase(std::remove(slot.handles.begin(), slot.handles.end(), handle), slot.handles.end());
		if (inserted) {
			hub.mNativeSlots.erase(slotIt);
		}
		throw;
	}
	return handle;
}

void ScriptFileWatcher::unwatch(Handle handle) {
	if (handle == 0) {
		return;
	}
	auto& hub = instance();
	std::lock_guard lock(hub.mMutex);
	if (hub.mCallbacks.erase(handle) == 0) {
		return;
	}
	for (auto slotIt = hub.mNativeSlots.begin(); slotIt != hub.mNativeSlots.end(); ++slotIt) {
		auto& slot = slotIt->second;
		slot.handles.erase(std::remove(slot.handles.begin(), slot.handles.end(), handle), slot.handles.end());
		if (slot.handles.empty()) {
			if (slot.watchId.valid()) {
				RESOURCES::FileWatcher::getInstance()->remove(slot.vfsPath, slot.watchId);
			}
			hub.mNativeSlots.erase(slotIt);
			break;
		}
	}
}

void ScriptFileWatcher::dispatchNative(const std::string& nativePath) {
	std::vector<ReloadFn> reloads;
	{
		std::lock_guard lock(mMutex);
		const auto slotIt = mNativeSlots.find(nativePath);
		if (slotIt == mNativeSlots.end()) {
			return;
		}
		for (const Handle handle : slotIt->second.handles) {
			if (auto cbIt = mCallbacks.find(handle); cbIt != mCallbacks.end() && cbIt->second.reload) {
				reloads.push_back(cbIt->second.reload);
			}
		}
	}
	for (const auto& reload : reloads) {
		reload();
	}
}
}
#endif
