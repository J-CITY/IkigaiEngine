#pragma once
#include "scriptModule/scriptSource.h"

namespace IKIGAI::SCRIPTING {
std::optional<std::string> readScriptFromVfs(const std::string& path);

#if defined(USE_FILE_WATCHER)
class ScriptFileWatcher;
ScriptFileWatcher& scriptFileWatcher();
#endif
}
