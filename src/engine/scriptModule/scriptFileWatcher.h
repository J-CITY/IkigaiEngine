#pragma once
#if defined(USE_FILE_WATCHER)
#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/fileWatcher.h"
#include "resourceModule/serviceManager.h"
#include "utilsModule/event.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace IKIGAI::SCRIPTING {
class ScriptFileWatcher {
public:
	using Handle = std::uint64_t;
	using ReloadFn = std::function<void()>;

	static Handle watch(const std::string& vfsPath, ReloadFn onModified) {
		auto& fs = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
		const auto native = fs.getAbsolutePath(vfsPath);
		if (!native || native->empty() || !onModified) {
			return 0;
		}

		std::lock_guard lock(instance().mMutex);
		const Handle handle = ++instance().mNextHandle;
		instance().mCallbacks.emplace(handle, CallbackEntry { std::move(onModified) });

		auto& nativeSlot = instance().mNativeSlots[*native];
		nativeSlot.handles.push_back(handle);
		if (!nativeSlot.watchId.valid()) {
			const std::string nativePath = *native;
			nativeSlot.watchId = RESOURCES::FileWatcher::getInstance()->add(nativePath, [nativePath](RESOURCES::FileWatcher::FileStatus status) {
				if (status != RESOURCES::FileWatcher::FileStatus::MODIFIED) {
					return;
				}
				instance().dispatchNative(nativePath);
			});
		}
		return handle;
	}

	static void unwatch(Handle handle) {
		if (handle == 0) {
			return;
		}
		std::lock_guard lock(instance().mMutex);
		if (instance().mCallbacks.find(handle) == instance().mCallbacks.end()) {
			return;
		}
		instance().mCallbacks.erase(handle);

		for (auto slotIt = instance().mNativeSlots.begin(); slotIt != instance().mNativeSlots.end(); ++slotIt) {
			auto& slot = slotIt->second;
			slot.handles.erase(std::remove(slot.handles.begin(), slot.handles.end(), handle), slot.handles.end());
			if (slot.handles.empty()) {
				if (slot.watchId.valid()) {
					RESOURCES::FileWatcher::getInstance()->remove(slotIt->first, slot.watchId);
				}
				instance().mNativeSlots.erase(slotIt);
				break;
			}
		}
	}

private:
	struct CallbackEntry {
		ReloadFn reload;
	};
	struct NativeSlot {
		IKIGAI::EVENT::Event<RESOURCES::FileWatcher::FileStatus>::ID watchId {};
		std::vector<Handle> handles;
	};

	static ScriptFileWatcher& instance() {
		static ScriptFileWatcher hub;
		return hub;
	}

	void dispatchNative(const std::string& nativePath) {
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

	std::mutex mMutex;
	Handle mNextHandle = 0;
	std::unordered_map<Handle, CallbackEntry> mCallbacks;
	std::unordered_map<std::string, NativeSlot> mNativeSlots;
};
}
#endif
