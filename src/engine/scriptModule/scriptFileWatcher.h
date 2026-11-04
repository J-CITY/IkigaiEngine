#pragma once
#if defined(USE_FILE_WATCHER)
#include "resourceModule/fileWatcher.h"
#include "utilsModule/event.h"
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

	static Handle watch(const std::string& vfsPath, ReloadFn onModified);
	static void unwatch(Handle handle);

private:
	struct CallbackEntry {
		ReloadFn reload {};
	};
	struct NativeSlot {
		IKIGAI::EVENT::Event<RESOURCES::FileWatcher::FileStatus>::ID watchId {};
		std::string vfsPath {};
		std::vector<Handle> handles {};
	};

	static ScriptFileWatcher& instance();
	void dispatchNative(const std::string& nativePath);

	std::mutex mMutex {};
	Handle mNextHandle = 0;
	std::unordered_map<Handle, CallbackEntry> mCallbacks {};
	std::unordered_map<std::string, NativeSlot> mNativeSlots {};
};
}
#endif
