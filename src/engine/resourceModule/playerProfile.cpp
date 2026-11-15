#include "playerProfile.h"
#include "fileSystem/storage.h"
#include "utilsModule/serialization/jsonValueAdaptor.h"
#include <algorithm>
#include <serdepp/serde.hpp>

namespace IKIGAI::RESOURCES {
namespace {
	class UnsupportedProfileSchema : public std::runtime_error {
	public:
		UnsupportedProfileSchema()
			: std::runtime_error("Unsupported profile schema") { }
	};
	std::span<const uint8_t> Bytes(const std::string& text) {
		return { reinterpret_cast<const uint8_t*>(text.data()), text.size() };
	}
	struct ProfileDescriptor {
		int schemaVersion = 1;
		nlohmann::json data = nlohmann::json::object();
		template <class Context> constexpr static void serde(Context& context, ProfileDescriptor& value) {
			serde::serde_struct(context, value).field(&ProfileDescriptor::schemaVersion, "schemaVersion").field(&ProfileDescriptor::data, "data");
		}
	};
	ProfileDescriptor ParseProfile(const std::vector<uint8_t>& bytes) {
		auto value = nlohmann::json::parse(bytes);
		const auto& version = value.at("schemaVersion");
		if (!version.is_number_integer() || version != 1) {
			throw UnsupportedProfileSchema();
		}
		// Version 1 is the first persisted schema. Never reinterpret a future
		// version as corrupt data and silently recover an older backup.
		return serde::deserialize<ProfileDescriptor>(value);
	}
	std::string WriteProfile(const nlohmann::json& data) {
		return serde::serialize<nlohmann::json>(ProfileDescriptor { 1, data }).dump(2);
	}

}
ProfileHandle::ProfileHandle(std::shared_ptr<STORAGE::UserStorage> storage, std::string prefix)
	: mStorage(std::move(storage))
	, mPrefix(std::move(prefix)) { }
std::string ProfileHandle::path(const std::string& relative, bool directory) const {
	if (directory && relative.empty()) {
		return mPrefix;
	}
	const auto base = mStorage->root() / std::filesystem::u8path(mPrefix);
	const auto checked = STORAGE::Utf8(STORAGE::Child(base, relative).lexically_relative(base));
	if (checked == ".") {
		throw std::runtime_error("Expected a profile-relative file or directory");
	}
	return mPrefix + "/" + checked;
}
std::vector<uint8_t> ProfileHandle::readFile(const std::string& relative) const {
	return mStorage->read(path(relative));
}
std::shared_future<void> ProfileHandle::writeFile(const std::string& relative, std::span<const uint8_t> bytes) const {
	return writeFileAtomic(relative, bytes);
}
std::shared_future<void> ProfileHandle::writeFileAtomic(const std::string& relative, std::span<const uint8_t> bytes) const {
	mStorage->writeAtomic(path(relative), bytes);
	return commit();
}
bool ProfileHandle::exists(const std::string& relative) const {
	return mStorage->exists(path(relative));
}
std::vector<STORAGE::UserStorage::Entry> ProfileHandle::listFiles(const std::string& directory) const {
	return mStorage->list(path(directory, true));
}
std::shared_future<void> ProfileHandle::createDirectories(const std::string& relative) const {
	mStorage->createDirectories(path(relative));
	return commit();
}
std::shared_future<void> ProfileHandle::remove(const std::string& relative) const {
	mStorage->remove(path(relative));
	return commit();
}
std::shared_future<void> ProfileHandle::commit() const {
	return mStorage->commit();
}
void PlayerProfileService::validateId(const std::string& id) {
	if (id.empty() || id.size() > 64 || !std::all_of(id.begin(), id.end(), [](unsigned char c) {
			return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
		})) {
		throw std::runtime_error("Invalid profile ID");
	}
}
PlayerProfileService::PlayerProfileService(FileSystem& files, std::filesystem::path root)
	: PlayerProfileService(files, std::make_shared<STORAGE::UserStorage>(std::move(root))) { }
PlayerProfileService::PlayerProfileService(FileSystem& files, std::shared_ptr<STORAGE::UserStorage> storage)
	: mFiles(files)
	, mRoot(storage->root())
	, mStorage(std::move(storage)) {
	mStorage->createDirectories("profiles");
	mStorage->createDirectories("cache");
	mCacheMount = mFiles.mountNative((mRoot / "cache").generic_string(), "/cache");
}
PlayerProfileService::~PlayerProfileService() {
	mFiles.unmount(mUserMount);
	mFiles.unmount(mCacheMount);
}
std::vector<std::string> PlayerProfileService::list() const {
	std::vector<std::string> result;
	for (const auto& entry : mStorage->list("profiles")) {
		if (!entry.directory) {
			continue;
		}
		try {
			validateId(entry.name);
		} catch (...) {
			continue;
		}
		if (mStorage->exists("profiles/" + entry.name + "/profile.json") || mStorage->exists("profiles/" + entry.name + "/profile.json.bak")) {
			result.push_back(entry.name);
		}
	}
	return result;
}
std::shared_future<void> PlayerProfileService::create(const std::string& id, const nlohmann::json& data) {
	validateId(id);
	const auto prefix = "profiles/" + id;
	if (mStorage->exists(prefix)) {
		throw std::runtime_error("Profile already exists");
	}
	const auto text = WriteProfile(data);
	mStorage->writeAtomic(prefix + "/profile.json", Bytes(text));
	return commit();
}
void PlayerProfileService::select(const std::string& id) {
	validateId(id);
	const auto prefix = "profiles/" + id;
	std::vector<uint8_t> bytes;
	try {
		bytes = mStorage->read(prefix + "/profile.json");
		ParseProfile(bytes);
	} catch (const UnsupportedProfileSchema&) {
		throw;
	} catch (...) {
		bytes = mStorage->read(prefix + "/profile.json.bak");
		ParseProfile(bytes);
		mStorage->writeAtomic(prefix + "/profile.json", bytes);
	}
	auto mount = mFiles.mountStorage(mStorage, prefix, "/user");
	mFiles.unmount(mUserMount);
	mUserMount = mount;
	mSelected = id;
}
ProfileHandle PlayerProfileService::profile() const {
	if (mSelected.empty()) {
		throw std::runtime_error("No profile selected");
	}
	return ProfileHandle(mStorage, "profiles/" + mSelected);
}
nlohmann::json PlayerProfileService::load() const {
	const auto handle = profile();
	try {
		return ParseProfile(handle.readFile("profile.json")).data;
	} catch (const UnsupportedProfileSchema&) {
		throw;
	} catch (...) {
		return ParseProfile(handle.readFile("profile.json.bak")).data;
	}
}
std::shared_future<void> PlayerProfileService::save(const nlohmann::json& data) {
	const auto handle = profile();
	const auto prefix = "profiles/" + mSelected + "/profile.json";
	if (mStorage->exists(prefix)) {
		const auto old = mStorage->read(prefix);
		ParseProfile(old);
		mStorage->writeAtomic(prefix + ".bak", old);
	}
	const auto text = WriteProfile(data);
	return handle.writeFileAtomic("profile.json", Bytes(text));
}
std::shared_future<void> PlayerProfileService::saveData(const std::string& relative, std::span<const uint8_t> bytes) {
	STORAGE::Child(mRoot / "profiles" / mSelected / "data", relative);
	return profile().writeFileAtomic("data/" + relative, bytes);
}
std::vector<uint8_t> PlayerProfileService::loadData(const std::string& relative) const {
	STORAGE::Child(mRoot / "profiles" / mSelected / "data", relative);
	return profile().readFile("data/" + relative);
}
}
