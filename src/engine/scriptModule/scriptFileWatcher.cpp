#include "scriptFileWatcher.h"
#include <algorithm>

#if defined(USE_FILE_WATCHER)
namespace IKIGAI::SCRIPTING {
ScriptFileWatcher::ScriptFileWatcher(ResolvePath& resolvePath, Subscribe& subscribe)
	: resolvePath(resolvePath)
	, subscribe(subscribe) { }

ScriptFileWatcher::~ScriptFileWatcher() {
	for (auto& [path, slot] : mNativeSlots) {
		if (slot.unsubscribe) {
			slot.unsubscribe();
		}
	}
}

ScriptFileWatcher::Handle ScriptFileWatcher::watch(const std::string& vfsPath, ReloadFn onModified) {
	const auto native = resolvePath(vfsPath);
	if (!native || native->empty() || !onModified) {
		return 0;
	}

	std::lock_guard lock(mMutex);
	const Handle handle = ++mNextHandle;
	auto [slotIt, inserted] = mNativeSlots.try_emplace(*native);
	auto& slot = slotIt->second;
	try {
		mCallbacks.emplace(handle, CallbackEntry { std::move(onModified) });
		slot.handles.push_back(handle);
		if (!slot.unsubscribe) {
			slot.unsubscribe = subscribe(vfsPath, [this, nativePath = *native] { dispatchNative(nativePath); });
		}
	} catch (...) {
		mCallbacks.erase(handle);
		slot.handles.erase(std::remove(slot.handles.begin(), slot.handles.end(), handle), slot.handles.end());
		if (inserted) {
			mNativeSlots.erase(slotIt);
		}
		throw;
	}
	return handle;
}

void ScriptFileWatcher::unwatch(Handle handle) {
	if (handle == 0) {
		return;
	}
	std::lock_guard lock(mMutex);
	if (mCallbacks.erase(handle) == 0) {
		return;
	}
	for (auto slotIt = mNativeSlots.begin(); slotIt != mNativeSlots.end(); ++slotIt) {
		auto& slot = slotIt->second;
		slot.handles.erase(std::remove(slot.handles.begin(), slot.handles.end(), handle), slot.handles.end());
		if (slot.handles.empty()) {
			if (slot.unsubscribe) {
				slot.unsubscribe();
			}
			mNativeSlots.erase(slotIt);
			break;
		}
	}
}

void ScriptFileWatcher::dispatchNative(const std::string& nativePath) {
	std::vector<Handle> handles;
	{
		std::lock_guard lock(mMutex);
		const auto slotIt = mNativeSlots.find(nativePath);
		if (slotIt == mNativeSlots.end()) {
			return;
		}
		handles = slotIt->second.handles;
	}
	for (const Handle handle : handles) {
		ReloadFn reload;
		{
			std::lock_guard lock(mMutex);
			if (const auto callback = mCallbacks.find(handle); callback != mCallbacks.end()) {
				reload = callback->second.reload;
			}
		}
		if (reload) {
			reload();
		}
	}
}
}
#endif
