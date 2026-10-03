#include "fileSystem.h"
#include "vfspp/VFS.h"
#ifdef USE_SDL
#include "sdlFileSystem.h"
#endif
#ifdef OCULUS
#include "androidAssetFileSystem.h"
#include <android/asset_manager.h>
#endif
#include "utilsModule/exeptions.h"
#include <filesystem>

namespace {
std::string NormalizeVirtualPath(std::string path) {
	for (char& ch : path) {
		if (ch == '\\') {
			ch = '/';
		}
	}
	if (path.empty() || path.front() != '/') {
		path.insert(path.begin(), '/');
	}
	return path;
}
}

namespace IKIGAI::RESOURCES {
	class FileSystemInternal {
	public:
		vfspp::VirtualFileSystemPtr mVFS;
		FileSystemInternal() : mVFS(new vfspp::VirtualFileSystem()) {}
	};

	class FileInternal {
	public:
		vfspp::IFilePtr mFile;
		FileInternal(vfspp::IFilePtr file) : mFile(file) {}
	};
}

IKIGAI::RESOURCES::File::File(std::unique_ptr<FileInternal> internal): mInternal(std::move(internal)) {
}

bool IKIGAI::RESOURCES::File::isValid() const {
	return mInternal->mFile != nullptr && mInternal->mFile->IsOpened();
}

std::string IKIGAI::RESOURCES::File::getFileExtension() const {
    if (!mInternal->mFile) return "";
	return mInternal->mFile->GetEntryInfo().Extension();
}

std::string IKIGAI::RESOURCES::File::getFileName() const {
    if (!mInternal->mFile) return "";
	return mInternal->mFile->GetEntryInfo().Filename();
}

std::string IKIGAI::RESOURCES::File::getAbsolutePath() const {
    if (!mInternal->mFile) return "";
	return mInternal->mFile->GetEntryInfo().NativePath();
}

bool IKIGAI::RESOURCES::File::isDir() const {
    if (!mInternal->mFile) return false;
	return mInternal->mFile->GetEntryInfo().IsDirectory();
}

bool IKIGAI::RESOURCES::File::isReadOnly() const {
    if (!mInternal->mFile) return true;
	return mInternal->mFile->IsReadOnly();
}

size_t IKIGAI::RESOURCES::File::getSize() const {
    if (!mInternal->mFile) return 0;
	return mInternal->mFile->Size();
}

bool IKIGAI::RESOURCES::File::isOpened() const {
    if (!mInternal->mFile) return false;
	return mInternal->mFile->IsOpened();
}

void IKIGAI::RESOURCES::File::open(FileMode mode) const {
    if (mInternal->mFile) {
	    (void)mInternal->mFile->Open(static_cast<vfspp::IFile::FileMode>(mode));
    }
}

void IKIGAI::RESOURCES::File::close() const {
    if (mInternal->mFile) {
	    mInternal->mFile->Close();
    }
}

std::vector<uint8_t> IKIGAI::RESOURCES::File::read(size_t sz) {
	std::vector<uint8_t> data;
    if (mInternal->mFile) {
	    mInternal->mFile->Read(data, sz);
    }
	return data;
}

std::vector<uint8_t> IKIGAI::RESOURCES::File::read() {
	return read(getSize());
}

std::string IKIGAI::RESOURCES::File::readStr() {
	auto data = read();
	return std::string(data.begin(), data.end());
}

void IKIGAI::RESOURCES::File::read(uint8_t* data, size_t sz) {
    if (mInternal->mFile) {
        std::span<uint8_t> buffer(data, sz);
	    mInternal->mFile->Read(buffer);
    }
}

void IKIGAI::RESOURCES::File::write(const uint8_t* data, size_t sz) {
    if (mInternal->mFile) {
        std::span<const uint8_t> buffer(data, sz);
	    mInternal->mFile->Write(buffer);
    }
}


IKIGAI::RESOURCES::FileSystem::FileSystem(): mInternal(std::make_unique<FileSystemInternal>()) {

}

IKIGAI::RESOURCES::FileSystem::~FileSystem() = default;

void IKIGAI::RESOURCES::FileSystem::addNativeFileSystem(const std::string& path, const std::string& pathInFs) {
	vfspp::IFileSystemPtr fs(new vfspp::NativeFileSystem(pathInFs, path));
	(void)fs->Initialize();
	mInternal->mVFS->AddFileSystem(pathInFs, fs);
}

void IKIGAI::RESOURCES::FileSystem::addZipFileSystem(const std::string& path, const std::string& pathInFs) {
	vfspp::IFileSystemPtr fs(new vfspp::ZipFileSystem(pathInFs, path));
	(void)fs->Initialize();
	mInternal->mVFS->AddFileSystem(pathInFs, fs);
}

void IKIGAI::RESOURCES::FileSystem::addMemoryFileSystem(const std::string& pathInFs) {
	vfspp::IFileSystemPtr fs(new vfspp::MemoryFileSystem(pathInFs));
	(void)fs->Initialize();
	mInternal->mVFS->AddFileSystem(pathInFs, fs);
}

void IKIGAI::RESOURCES::FileSystem::addSdlFileSystem(const std::string& path, const std::string& pathInFs) {
#ifdef USE_SDL
	vfspp::IFileSystemPtr fs(new SdlFileSystem(pathInFs, path));
	(void)fs->Initialize();
	mInternal->mVFS->AddFileSystem(pathInFs, fs);
#else
	(void)path;
	(void)pathInFs;
#endif
}

void IKIGAI::RESOURCES::FileSystem::addAndroidAssetFileSystem(const std::string& path, const std::string& pathInFs, void* assetManager, void* androidApp) {
#ifdef OCULUS
	vfspp::IFileSystemPtr fs(new AndroidAssetFileSystem(pathInFs, path,
		static_cast<AAssetManager*>(assetManager), androidApp));
	(void)fs->Initialize();
	mInternal->mVFS->AddFileSystem(pathInFs, fs);
#else
	(void)path;
	(void)pathInFs;
	(void)assetManager;
	(void)androidApp;
#endif
}

bool IKIGAI::RESOURCES::FileSystem::isValid(const std::string& path) const {
	auto entry = mInternal->mVFS->GetEntry(NormalizeVirtualPath(path));
	return entry.has_value();
}

bool IKIGAI::RESOURCES::FileSystem::isFileExist(const std::string& path) const {
	return mInternal->mVFS->IsFileExists(NormalizeVirtualPath(path));
}

std::string IKIGAI::RESOURCES::FileSystem::getFileExtension(const std::string& path) const {
	auto entry = mInternal->mVFS->GetEntry(NormalizeVirtualPath(path));
	return entry ? entry->Extension() : "";
}

std::string IKIGAI::RESOURCES::FileSystem::getFileName(const std::string& path) const {
	auto entry = mInternal->mVFS->GetEntry(NormalizeVirtualPath(path));
	return entry ? entry->Filename() : "";
}

std::optional<std::string> IKIGAI::RESOURCES::FileSystem::getAbsolutePath(const std::string& path) const {
	auto entry = mInternal->mVFS->GetEntry(NormalizeVirtualPath(path));
	if (entry) return entry->NativePath();
    return std::nullopt;
}

bool IKIGAI::RESOURCES::FileSystem::isDir(const std::string& path) const {
	return mInternal->mVFS->IsDirectoryExists(NormalizeVirtualPath(path));
}

IKIGAI::RESOURCES::FileSystem::FileTime IKIGAI::RESOURCES::FileSystem::lastWriteTime(const std::string& path) {
	auto pathOpt = getAbsolutePath(path);
	if (!pathOpt) {
		return {};
	}
	std::error_code ec;
	const auto time = std::filesystem::last_write_time(*pathOpt, ec);
	if (ec) {
		return {};
	}
	return time;
}

uintmax_t IKIGAI::RESOURCES::FileSystem::fileSize(const std::string& path) {
	auto pathOpt = getAbsolutePath(path);
	if (pathOpt) {
		std::error_code ec;
		const auto size = std::filesystem::file_size(*pathOpt, ec);
		if (!ec) {
			return size;
		}
	}
	auto file = getFile(path, FileMode::READ);
	if (file && file->isValid()) {
		return file->getSize();
	}
	return 0;
}

std::shared_ptr<IKIGAI::RESOURCES::File> IKIGAI::RESOURCES::FileSystem::getFile(const std::string& path, FileMode mode) {
	vfspp::IFilePtr file = mInternal->mVFS->OpenFile(NormalizeVirtualPath(path), static_cast<vfspp::IFile::FileMode>(mode));
	return std::make_shared<File>(std::make_unique<FileInternal>(file));
}

std::optional<std::string> IKIGAI::RESOURCES::FileSystem::getFilePath(const std::string& path) const {
    auto entry = mInternal->mVFS->GetEntry(path);
    if (entry) return entry->VirtualPath();
	return std::nullopt;
}

std::vector<IKIGAI::RESOURCES::FileSystem::DirectoryEntry> IKIGAI::RESOURCES::FileSystem::listDirectory(const std::string& path) const {
	std::vector<DirectoryEntry> result;
	const auto entries = mInternal->mVFS->ListAllEntries(NormalizeVirtualPath(path), false, false);
	result.reserve(entries.size());
	for (const auto& entry : entries) {
		DirectoryEntry item;
		item.path = entry.VirtualPath();
		item.name = entry.Filename();
		item.isDirectory = entry.IsDirectory();
		result.push_back(std::move(item));
	}
	return result;
}
