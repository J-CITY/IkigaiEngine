#include "userStorage.h"
#include "storage.h"
#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>
#ifdef USE_SDL
#include <SDL3/SDL.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace IKIGAI::RESOURCES::STORAGE {
namespace {
	std::string Checked(const std::filesystem::path& root, const std::string& path, bool directory = false) {
		if (directory && path.empty())
			return "";
		return Utf8(Child(root, path).lexically_relative(root));
	}
#ifdef USE_SDL
	struct StorageSession {
		SDL_Storage* value;
		explicit StorageSession(const std::filesystem::path& root, const std::string& org, const std::string& app) {
			const auto utf8 = root.u8string();
			value = org.empty() ? SDL_OpenFileStorage(reinterpret_cast<const char*>(utf8.c_str())) : SDL_OpenUserStorage(org.c_str(), app.c_str(), 0);
			if (!value)
				throw std::runtime_error(std::string("Open storage: ") + SDL_GetError());
			const auto deadline = SDL_GetTicks() + 5000;
			while (!SDL_StorageReady(value)) {
#ifdef __EMSCRIPTEN__
				SDL_CloseStorage(value);
				value = nullptr;
				throw std::runtime_error("Storage is not ready");
#else
				if (SDL_GetTicks() >= deadline) {
					SDL_CloseStorage(value);
					value = nullptr;
					throw std::runtime_error("Storage readiness timeout");
				}
				SDL_Delay(1);
#endif
			}
		}
		~StorageSession() {
			if (value)
				SDL_CloseStorage(value);
		}
		void check(bool success) {
			if (!success)
				throw std::runtime_error(std::string("Storage operation: ") + SDL_GetError());
		}
		void close() {
			auto* handle = value;
			value = nullptr;
			check(SDL_CloseStorage(handle));
		}
		void directories(const std::string& path) {
			std::string prefix;
			for (const auto& part : std::filesystem::u8path(path)) {
				prefix += (prefix.empty() ? "" : "/") + Utf8(part);
				check(SDL_CreateStorageDirectory(value, prefix.c_str()));
			}
		}
	};
#endif
}
UserStorage::UserStorage(std::filesystem::path root)
	: mRoot(std::filesystem::absolute(std::move(root)).lexically_normal()) {
	std::filesystem::create_directories(mRoot);
}
UserStorage::UserStorage(std::string org, std::string app)
	: UserStorage(AppDataPath(org, app)) {
	mOrganization = std::move(org);
	mApplication = std::move(app);
}
std::vector<uint8_t> UserStorage::read(const std::string& path, uint64_t limit) const {
	std::lock_guard lock(mMutex);
	const auto key = Checked(mRoot, path);
#ifdef USE_SDL
	StorageSession session(mRoot, mOrganization, mApplication);
	Uint64 size = 0;
	session.check(SDL_GetStorageFileSize(session.value, key.c_str(), &size));
	if (size > limit || size > std::numeric_limits<size_t>::max())
		throw std::runtime_error("Storage file exceeds size limit");
	std::vector<uint8_t> result(static_cast<size_t>(size));
	uint8_t empty = 0;
	session.check(SDL_ReadStorageFile(session.value, key.c_str(), result.empty() ? &empty : result.data(), size));
	session.close();
	return result;
#else
	return Read(mRoot / std::filesystem::u8path(key), limit);
#endif
}
void UserStorage::write(const std::string& path, std::span<const uint8_t> data) {
	writeAtomic(path, data);
}
void UserStorage::writeAtomic(const std::string& path, std::span<const uint8_t> data) {
	std::lock_guard lock(mMutex);
	const auto key = Checked(mRoot, path);
#ifdef USE_SDL
	StorageSession session(mRoot, mOrganization, mApplication);
	session.directories(Utf8(std::filesystem::u8path(key).parent_path()));
	static std::atomic<uint64_t> sequence { 0 };
	const auto temporary = key + ".tmp-" + std::to_string(SDL_GetTicksNS()) + "-" + std::to_string(sequence++);
	uint8_t empty = 0;
	try {
		session.check(SDL_WriteStorageFile(session.value, temporary.c_str(), data.empty() ? &empty : data.data(), data.size()));
		session.check(SDL_RenameStoragePath(session.value, temporary.c_str(), key.c_str()));
	} catch (...) {
		SDL_RemoveStoragePath(session.value, temporary.c_str());
		throw;
	}
	session.close();
#else
	WriteAtomic(mRoot / std::filesystem::u8path(key), data);
#endif
}
bool UserStorage::exists(const std::string& path) const {
	std::lock_guard lock(mMutex);
	const auto key = Checked(mRoot, path, true);
#ifdef USE_SDL
	StorageSession session(mRoot, mOrganization, mApplication);
	SDL_PathInfo info { };
	const bool result = SDL_GetStoragePathInfo(session.value, key.c_str(), &info);
	session.close();
	return result;
#else
	return std::filesystem::exists(mRoot / std::filesystem::u8path(key));
#endif
}
bool UserStorage::isDirectory(const std::string& path) const {
	std::lock_guard lock(mMutex);
	const auto key = Checked(mRoot, path, true);
#ifdef USE_SDL
	StorageSession session(mRoot, mOrganization, mApplication);
	SDL_PathInfo info { };
	const bool result = SDL_GetStoragePathInfo(session.value, key.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
	session.close();
	return result;
#else
	return std::filesystem::is_directory(mRoot / std::filesystem::u8path(key));
#endif
}
std::vector<UserStorage::Entry> UserStorage::list(const std::string& path) const {
	std::lock_guard lock(mMutex);
	const auto key = Checked(mRoot, path, true);
	std::vector<Entry> result;
#ifdef USE_SDL
	StorageSession session(mRoot, mOrganization, mApplication);
	std::vector<std::string> names;
	session.check(SDL_EnumerateStorageDirectory(
		session.value, key.c_str(),
		[](void* data, const char*, const char* name) {
			// No exceptions may cross the C callback boundary.
			try {
				static_cast<std::vector<std::string>*>(data)->emplace_back(name);
				return SDL_ENUM_CONTINUE;
			} catch (...) {
				return SDL_ENUM_FAILURE;
			}
		},
		&names));
	for (const auto& name : names) {
		SDL_PathInfo info { };
		const auto child = Checked(mRoot, key.empty() ? name : key + "/" + name);
		session.check(SDL_GetStoragePathInfo(session.value, child.c_str(), &info));
		result.push_back({ name, info.type == SDL_PATHTYPE_DIRECTORY });
	}
	session.close();
#else
	for (const auto& entry : std::filesystem::directory_iterator(mRoot / std::filesystem::u8path(key))) {
		const auto name = Utf8(entry.path().filename());
		Checked(mRoot, key.empty() ? name : key + "/" + name);
		result.push_back({ name, entry.is_directory() });
	}
#endif
	std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
	return result;
}
void UserStorage::createDirectories(const std::string& path) {
	std::lock_guard lock(mMutex);
	const auto key = Checked(mRoot, path, true);
#ifdef USE_SDL
	StorageSession session(mRoot, mOrganization, mApplication);
	session.directories(key);
	session.close();
#else
	std::filesystem::create_directories(mRoot / std::filesystem::u8path(key));
#endif
}
void UserStorage::remove(const std::string& path) {
	std::lock_guard lock(mMutex);
	const auto key = Checked(mRoot, path);
	if (key == ".")
		throw std::runtime_error("Cannot remove storage root");
#ifdef USE_SDL
	StorageSession session(mRoot, mOrganization, mApplication);
	session.check(SDL_RemoveStoragePath(session.value, key.c_str()));
	session.close();
#else
	if (!std::filesystem::remove(mRoot / std::filesystem::u8path(key)))
		throw std::runtime_error("Storage path does not exist");
#endif
}
std::shared_future<void> ReadyCommit() {
	std::promise<void> value;
	value.set_value();
	return value.get_future().share();
}
#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE void ikigai_storage_commit_done(void* pointer, int failed) {
	std::unique_ptr<std::promise<void>> promise(static_cast<std::promise<void>*>(pointer));
	if (failed)
		promise->set_exception(std::make_exception_ptr(std::runtime_error("IndexedDB commit failed")));
	else
		promise->set_value();
}
#endif
std::shared_future<void> CommitPersistent() {
#ifdef __EMSCRIPTEN__
	auto promise = std::make_unique<std::promise<void>>();
	auto result = promise->get_future().share();
	EM_ASM(
		{
			const pointer = $0;
			if (!Module['ikigaiStorageCommit']) {
				_ikigai_storage_commit_done(pointer, 1);
				return;
			}
			Module['ikigaiStorageCommit']().then(
				function() { _ikigai_storage_commit_done(pointer, 0); }, function() { _ikigai_storage_commit_done(pointer, 1); });
		},
		promise.release());
	return result;
#else
	return ReadyCommit();
#endif
}
std::shared_future<void> UserStorage::commit() const {
	return CommitPersistent();
}
}
