#pragma once
#include "fileSystem/fileSystem.h"
#include "fileSystem/userStorage.h"
#include <nlohmann/json.hpp>

namespace IKIGAI::RESOURCES {
class ProfileHandle {
	std::shared_ptr<STORAGE::UserStorage> mStorage;
	std::string mPrefix;
	std::string path(const std::string& relative, bool directory = false) const;

public:
	ProfileHandle(std::shared_ptr<STORAGE::UserStorage> storage, std::string prefix);
	std::vector<uint8_t> readFile(const std::string& relative) const;
	std::shared_future<void> writeFile(const std::string& relative, std::span<const uint8_t> bytes) const;
	std::shared_future<void> writeFileAtomic(const std::string& relative, std::span<const uint8_t> bytes) const;
	bool exists(const std::string& relative) const;
	std::vector<STORAGE::UserStorage::Entry> listFiles(const std::string& directory = "") const;
	std::shared_future<void> createDirectories(const std::string& relative) const;
	std::shared_future<void> remove(const std::string& relative) const;
	std::shared_future<void> commit() const;
};
class PlayerProfileService {
	FileSystem& mFiles;
	std::filesystem::path mRoot;
	std::shared_ptr<STORAGE::UserStorage> mStorage;
	FileSystem::MountId mUserMount = 0;
	FileSystem::MountId mCacheMount = 0;
	std::string mSelected;
	static void validateId(const std::string& id);

public:
	PlayerProfileService(FileSystem& files, std::filesystem::path root);
	PlayerProfileService(FileSystem& files, std::shared_ptr<STORAGE::UserStorage> storage);
	~PlayerProfileService();
	PlayerProfileService(const PlayerProfileService&) = delete;
	PlayerProfileService& operator=(const PlayerProfileService&) = delete;
	const std::filesystem::path& root() const {
		return mRoot;
	}
	const std::string& selected() const {
		return mSelected;
	}
	std::vector<std::string> list() const;
	std::shared_future<void> create(const std::string& id, const nlohmann::json& data = nlohmann::json::object());
	void select(const std::string& id);
	nlohmann::json load() const;
	std::shared_future<void> save(const nlohmann::json& data);
	std::shared_future<void> saveData(const std::string& relative, std::span<const uint8_t> bytes);
	std::vector<uint8_t> loadData(const std::string& relative) const;
	ProfileHandle profile() const;
	std::vector<uint8_t> readFile(const std::string& relative) const {
		return profile().readFile(relative);
	}
	std::shared_future<void> writeFile(const std::string& relative, std::span<const uint8_t> bytes) {
		return profile().writeFile(relative, bytes);
	}
	std::shared_future<void> writeFileAtomic(const std::string& relative, std::span<const uint8_t> bytes) {
		return profile().writeFileAtomic(relative, bytes);
	}
	std::vector<STORAGE::UserStorage::Entry> listFiles(const std::string& directory = "") const {
		return profile().listFiles(directory);
	}
	std::shared_future<void> commit() const {
		return mStorage->commit();
	}
};
}
