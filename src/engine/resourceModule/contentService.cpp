#include "contentService.h"
#include "fileSystem/storage.h"
#include "fileSystem/userStorage.h"
#include "resourceResolver.h"
#include "utilsModule/serialization/jsonAdapters.h"
#include "utilsModule/serialization/strictJsonFields.h"
#include <regex>
#include <serdepp/serde.hpp>
#include <set>

namespace IKIGAI::RESOURCES {
namespace {
	constexpr int ContentSchemaV1 = 1;
	constexpr int EngineVersion = 1;
	constexpr std::size_t MaxArchives = 128;
	constexpr uint64_t MaxArchiveSize = uint64_t { 1 } << 34;
}

struct ContentArchive {
	std::string path = {};
	std::string sha256 = {};
	uint64_t size = 0;
	std::vector<std::string> deletedPaths = {};
	template <class Context> constexpr static void serde(Context& context, ContentArchive& value) {
		serde::serde_struct(context, value)
			.field(&ContentArchive::path, "path")
			.field(&ContentArchive::sha256, "sha256")
			.field(&ContentArchive::size, "size", DATA::JsonIntegerRange { 0, MaxArchiveSize, "Invalid archive size" })
			.field(&ContentArchive::deletedPaths, "deletedPaths", serde::attribute::default_ { std::vector<std::string> {} },
				DATA::JsonContainer { nlohmann::json::value_t::array });
	}
};
struct ContentManifestV1 {
	int schemaVersion = ContentSchemaV1;
	int minEngineVersion = EngineVersion;
	std::string platform = {};
	std::string releaseId = {};
	std::vector<ContentArchive> archives = {};
	std::map<std::string, std::string> files = {};
	template <class Context> constexpr static void serde(Context& context, ContentManifestV1& value) {
		serde::serde_struct(context, value)
			.field(&ContentManifestV1::schemaVersion, "schemaVersion",
				DATA::JsonIntegerRange { ContentSchemaV1, ContentSchemaV1, "Unsupported content schema version" })
			.field(&ContentManifestV1::minEngineVersion, "minEngineVersion",
				DATA::JsonIntegerRange { 0, EngineVersion, "Unsupported content engine version" })
			.field(&ContentManifestV1::platform, "platform")
			.field(&ContentManifestV1::releaseId, "releaseId")
			.field(&ContentManifestV1::archives, "archives", DATA::JsonContainer { nlohmann::json::value_t::array, 1, MaxArchives })
			.field(&ContentManifestV1::files, "files", DATA::JsonContainer { nlohmann::json::value_t::object });
	}
};

ContentService::ContentService(FileSystem& files, std::filesystem::path root, std::string platform)
	: mFiles(files)
	, mRoot(std::move(root))
	, mPlatform(std::move(platform)) { }

ContentManifestV1 ContentService::validate(nlohmann::json source) const {
	auto manifest = serde::deserialize<ContentManifestV1>(source);
	if (manifest.platform != "any" && manifest.platform != mPlatform) {
		throw std::runtime_error("Content platform mismatch");
	}
	if (!std::regex_match(manifest.releaseId, std::regex("[a-zA-Z0-9_-]{1,80}"))) {
		throw std::runtime_error("Invalid release ID");
	}
	for (const auto& archive : manifest.archives) {
		if (!std::regex_match(archive.sha256, std::regex("[0-9a-f]{64}"))) {
			throw std::runtime_error("Invalid archive identity");
		}
		if (archive.path != "archives/" + archive.sha256 + ".zip") {
			throw std::runtime_error("Invalid archive path");
		}
		for (const auto& item : archive.deletedPaths) {
			FileSystem::normalizePath(item);
		}
	}
	for (const auto& [key, hash] : manifest.files) {
		const auto path = FileSystem::normalizePath(key);
		if (path == "/" || path.starts_with("/user/") || path.starts_with("/cache/") || !std::regex_match(hash, std::regex("[0-9a-f]{64}"))) {
			throw std::runtime_error("Invalid file catalog entry");
		}
	}
	return manifest;
}

void ContentService::verify(const ContentManifestV1& manifest) const {
	FileSystem candidate;
	for (const auto& archive : manifest.archives) {
		const auto path = STORAGE::Child(mRoot, archive.path);
		if (std::filesystem::file_size(path) != archive.size || STORAGE::Sha256(path) != archive.sha256) {
			throw std::runtime_error("Archive integrity mismatch");
		}
		candidate.mountZip(path.generic_string(), "/", archive.deletedPaths);
	}
	// Read every visible file to verify ZIP CRC/decompression and the catalog.
	std::set<std::string> visible;
	std::function<void(std::string)> walk = [&](const auto& dir) {
		for (const auto& entry : candidate.listDirectory(dir)) {
			if (entry.isDirectory) {
				walk(entry.path);
			}
			else {
				visible.insert(entry.path.substr(1));
			}
		}
	};
	walk("/");
	if (visible.size() != manifest.files.size()) {
		throw std::runtime_error("Release file count mismatch");
	}
	for (const auto& name : visible) {
		if (!manifest.files.contains(name)) {
			throw std::runtime_error("Unlisted release file");
		}
		auto file = candidate.getFile(name);
		const auto bytes = file->read();
		if (bytes.size() != file->getSize()) {
			throw std::runtime_error("Corrupt archive entry");
		}
		if (STORAGE::Sha256(std::span<const uint8_t>(bytes)) != manifest.files.at(name)) {
			throw std::runtime_error("Release file hash mismatch: " + name);
		}
	}
	if (!candidate.isFileExist("/resource-map.json")) {
		throw std::runtime_error("Missing resource map");
	}
	const auto mapping = nlohmann::json::parse(candidate.getFile("/resource-map.json")->readStr());
	ResourceResolver resolver(candidate);
	for (const auto& [key, value] : mapping.at("textures").items()) {
		resolver.texture(key);
	}
}

void ContentService::stageLocal(const std::filesystem::path& path) {
	const auto manifest = validate(nlohmann::json::parse(STORAGE::Read(path)));
	for (const auto& archive : manifest.archives) {
		const auto relative = archive.path;
		const auto source = STORAGE::Child(path.parent_path(), relative);
		const auto target = STORAGE::Child(mRoot, relative);
		if (std::filesystem::file_size(source) != archive.size || STORAGE::Sha256(source) != archive.sha256) {
			throw std::runtime_error("Source archive integrity mismatch");
		}
		std::filesystem::create_directories(target.parent_path());
		if (std::filesystem::is_regular_file(target) && std::filesystem::file_size(target) == archive.size
			&& STORAGE::Sha256(target) == archive.sha256) {
			continue;
		}
		if (std::filesystem::weakly_canonical(source) != std::filesystem::weakly_canonical(target)) {
			auto temporary = target;
			temporary += ".install";
			std::filesystem::copy_file(source, temporary, std::filesystem::copy_options::overwrite_existing);
			if (std::filesystem::exists(target)) {
				std::filesystem::remove(target);
			}
			std::filesystem::rename(temporary, target);
		}
	}
	verify(manifest);
	STORAGE::WriteTextAtomic(mRoot / "releases" / (manifest.releaseId + ".json"), serde::serialize<nlohmann::json>(manifest).dump(2));
}

std::vector<NETWORK::DownloadHandle> ContentService::download(
	NETWORK::DownloadManager& manager, const nlohmann::json& trustedManifest, const std::string& baseUrl, bool allowLocalhost) {
	const auto manifest = validate(trustedManifest);
	std::vector<NETWORK::DownloadHandle> result;
	for (const auto& archive : manifest.archives) {
		const auto relative = archive.path;
		const auto hash = archive.sha256;
		NETWORK::DownloadRequest request;
		request.destination = STORAGE::Child(mRoot, relative);
		request.size = archive.size;
		request.identity = hash;
		request.http.url = baseUrl + (baseUrl.ends_with('/') ? "" : "/") + relative;
		request.http.allowInsecureLocalhost = allowLocalhost;
		request.verify = [hash](const auto& path) { return STORAGE::Sha256(path) == hash; };
		request.commit = [] { return STORAGE::CommitPersistent(); };
		result.push_back(manager.download(std::move(request)));
	}
	return result;
}

std::shared_future<void> ContentService::activate(const nlohmann::json& source) {
	const auto manifest = validate(source);
	verify(manifest);
	const auto nextSnapshot = serde::serialize<nlohmann::json>(manifest);
	std::vector<FileSystem::MountId> next;
	try {
		for (const auto& archive : manifest.archives) {
			next.push_back(mFiles.mountZip(STORAGE::Child(mRoot, archive.path).generic_string(), "/", archive.deletedPaths));
		}
		const auto active = mRoot / "active.json";
		if (std::filesystem::is_regular_file(active)) {
			try {
				const auto bytes = STORAGE::Read(active);
				const auto old = nlohmann::json::parse(bytes);
				verify(validate(old));
				if (old != nextSnapshot) {
					STORAGE::WriteAtomic(mRoot / "previous.json", bytes);
				}
			} catch (const std::exception&) { /* Preserve the known-good previous snapshot during recovery. */
			}
		}
		STORAGE::WriteTextAtomic(active, nextSnapshot.dump(2));
	} catch (...) {
		for (auto id : next) {
			mFiles.unmount(id);
		}
		throw;
	}
	for (auto id : mMounts) {
		mFiles.unmount(id);
	}
	mMounts = std::move(next);
	mRelease = manifest.releaseId;
	return STORAGE::CommitPersistent();
}

bool ContentService::restore() {
	for (const auto& name : { "active.json", "previous.json" }) {
		const auto path = mRoot / name;
		if (!std::filesystem::is_regular_file(path)) {
			continue;
		}
		try {
			const auto manifest = nlohmann::json::parse(STORAGE::Read(path));
			activate(manifest);
			return true;
		} catch (const std::exception&) { }
	}
	if (std::filesystem::exists(mRoot / "active.json") || std::filesystem::exists(mRoot / "previous.json")) {
		throw std::runtime_error("No valid installed content release");
	}
	return false;
}
}
