#include "scriptResourceHost.h"

#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#if defined(USE_FILE_WATCHER)
#include "resourceModule/fileWatcher.h"
#include "scriptModule/scriptFileWatcher.h"
#endif

namespace IKIGAI::SCRIPTING {
std::optional<std::string> readScriptFromVfs(const std::string& path) {
	auto file = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().getFile(path);
	if (!file || !file->isValid()) {
		return std::nullopt;
	}
	return file->readStr();
}

#if defined(USE_FILE_WATCHER)
namespace {
	std::optional<std::string> resolveNativePath(const std::string& path) {
		return RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().getAbsolutePath(path);
	}

	ScriptFileWatcher::Unsubscribe subscribeToModifications(const std::string& path, ScriptFileWatcher::ReloadFn reload) {
		using RESOURCES::FileWatcher;
		auto* watcher = FileWatcher::getInstance();
		const auto id = watcher->add(path, [reload = std::move(reload)](FileWatcher::FileStatus status) {
			if (status == FileWatcher::FileStatus::MODIFIED) {
				reload();
			}
		});
		if (!id.valid()) {
			return {};
		}
		try {
			return [watcher, path, id] { watcher->remove(path, id); };
		} catch (...) {
			watcher->remove(path, id);
			throw;
		}
	}
}

ScriptFileWatcher& scriptFileWatcher() {
	static ScriptFileWatcher watcher(resolveNativePath, subscribeToModifications);
	return watcher;
}
#endif
}
