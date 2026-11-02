#pragma once
#include <algorithm>
#include <string>
#include <string_view>

namespace IKIGAI::SCRIPTING {
inline std::string NormalizeScriptVfsPath(std::string path, std::string_view extension, std::string_view rootPrefix) {
	std::replace(path.begin(), path.end(), '\\', '/');
	while (!path.empty() && path.front() == '/') {
		path.erase(path.begin());
	}
	if (!path.starts_with(rootPrefix)) {
		path.insert(0, rootPrefix);
	}
	if (!path.ends_with(extension)) {
		path += extension;
	}
	return "/" + path;
}

inline std::string LuaScriptVfsPath(const std::string& scriptName) {
	return NormalizeScriptVfsPath(scriptName, ".lua", "scripts/");
}

inline std::string IkigaiScriptVfsPath(const std::string& scriptName) {
	return NormalizeScriptVfsPath(scriptName, ".ik", "scripts/");
}
}
