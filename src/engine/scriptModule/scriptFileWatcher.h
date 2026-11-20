#pragma once
#if defined(USE_FILE_WATCHER)
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace IKIGAI::SCRIPTING {
class ScriptFileWatcher {
public:
	using Handle = std::uint64_t;
	using ReloadFn = std::function<void()>;
	using Unsubscribe = std::function<void()>;
	using ResolvePath = std::optional<std::string>(const std::string& vfsPath);
	using Subscribe = Unsubscribe(const std::string& vfsPath, ReloadFn onModified);

	ScriptFileWatcher(ResolvePath& resolvePath, Subscribe& subscribe);
	~ScriptFileWatcher();
	Handle watch(const std::string& vfsPath, ReloadFn onModified);
	void unwatch(Handle handle);

private:
	struct CallbackEntry {
		ReloadFn reload {};
	};
	struct NativeSlot {
		Unsubscribe unsubscribe {};
		std::vector<Handle> handles {};
	};

	void dispatchNative(const std::string& nativePath);

	ResolvePath& resolvePath;
	Subscribe& subscribe;
	std::mutex mMutex {};
	Handle mNextHandle = 0;
	std::unordered_map<Handle, CallbackEntry> mCallbacks {};
	std::unordered_map<std::string, NativeSlot> mNativeSlots {};
};
}
#endif
