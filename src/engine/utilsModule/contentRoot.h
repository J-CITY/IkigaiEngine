#pragma once

#include <coreModule/config.h>
#include <filesystem>
#include <optional>
#include <string>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace IKIGAI::UTILS {

inline std::string NormalizeRootPath(std::string path) {
	for (char& ch : path) {
		if (ch == '\\') {
			ch = '/';
		}
	}
	if (!path.empty() && path.back() != '/') {
		path += '/';
	}
	return path;
}

inline bool HasEngineAssetsTree(const std::filesystem::path& root) {
	const auto engineRoot = root / "assets" / "engine";
	return std::filesystem::is_directory(engineRoot);
}

inline std::optional<std::filesystem::path> FindContentRootFrom(const std::filesystem::path& start) {
	std::error_code ec;
	std::filesystem::path dir = std::filesystem::weakly_canonical(start, ec);
	if (ec) {
		dir = start;
	}
	for (int depth = 0; depth < 10; ++depth) {
		if (HasEngineAssetsTree(dir)) {
			return dir;
		}
		if (!dir.has_parent_path() || dir == dir.parent_path()) {
			break;
		}
		dir = dir.parent_path();
	}
	return std::nullopt;
}

inline std::filesystem::path ExecutablePath() {
#if defined(__APPLE__)
	uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size);
	std::string buffer(size, '\0');
	if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
		return {};
	}
	return std::filesystem::path(buffer.c_str());
#elif defined(_WIN32)
	wchar_t buffer[MAX_PATH]{};
	const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
	if (len == 0 || len >= MAX_PATH) {
		return {};
	}
	return std::filesystem::path(buffer);
#else
	std::error_code ec;
	const auto link = std::filesystem::read_symlink("/proc/self/exe", ec);
	if (!ec) {
		return link;
	}
	return {};
#endif
}

/** Sets Config::ROOT to a directory that contains assets/engine/ (trailing slash). */
inline void InitContentRoot() {
	if (!Config::ROOT.empty()) {
		return;
	}

	std::optional<std::filesystem::path> found;

	if (auto fromCwd = FindContentRootFrom(std::filesystem::current_path())) {
		found = fromCwd;
	}

	if (!found) {
		const auto exe = ExecutablePath();
		if (!exe.empty()) {
			found = FindContentRootFrom(exe.parent_path());
		}
	}

	if (found) {
		Config::ROOT = NormalizeRootPath(found->string());
	}
}

} // namespace IKIGAI::UTILS
