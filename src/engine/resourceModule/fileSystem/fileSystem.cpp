#include "fileSystem.h"
#include "vfspp/VFS.h"
#include "sdlFileSystem.h"
#include "utilsModule/exeptions.h"
#include <filesystem>

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
	return mInternal->mFile != nullptr;
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
	    mInternal->mFile->Open(static_cast<vfspp::IFile::FileMode>(mode));
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
	fs->Initialize();
	mInternal->mVFS->AddFileSystem(pathInFs, fs);
}

void IKIGAI::RESOURCES::FileSystem::addZipFileSystem(const std::string& path, const std::string& pathInFs) {
	vfspp::IFileSystemPtr fs(new vfspp::ZipFileSystem(pathInFs, path));
	fs->Initialize();
	mInternal->mVFS->AddFileSystem(pathInFs, fs);
}

void IKIGAI::RESOURCES::FileSystem::addMemoryFileSystem(const std::string& pathInFs) {
	vfspp::IFileSystemPtr fs(new vfspp::MemoryFileSystem(pathInFs));
	fs->Initialize();
	mInternal->mVFS->AddFileSystem(pathInFs, fs);
}

void IKIGAI::RESOURCES::FileSystem::addSdlFileSystem(const std::string& path, const std::string& pathInFs) {
	vfspp::IFileSystemPtr fs(new SdlFileSystem(pathInFs, path));
	fs->Initialize();
	mInternal->mVFS->AddFileSystem(pathInFs, fs);
}

bool IKIGAI::RESOURCES::FileSystem::isValid(const std::string& path) const {
	auto entry = mInternal->mVFS->GetEntryInfo(path);
	return entry.has_value();
}

bool IKIGAI::RESOURCES::FileSystem::isFileExist(const std::string& path) const {
	return mInternal->mVFS->IsFileExists(path);
}

std::string IKIGAI::RESOURCES::FileSystem::getFileExtension(const std::string& path) const {
	auto entry = mInternal->mVFS->GetEntryInfo(path);
	return entry ? entry->Extension() : "";
}

std::string IKIGAI::RESOURCES::FileSystem::getFileName(const std::string& path) const {
	auto entry = mInternal->mVFS->GetEntryInfo(path);
	return entry ? entry->Filename() : "";
}

std::optional<std::string> IKIGAI::RESOURCES::FileSystem::getAbsolutePath(const std::string& path) const {
	auto entry = mInternal->mVFS->GetEntryInfo(path);
	if (entry) return entry->NativePath();
    return std::nullopt;
}

bool IKIGAI::RESOURCES::FileSystem::isDir(const std::string& path) const {
	auto entry = mInternal->mVFS->GetEntryInfo(path);
	return entry ? entry->IsDirectory() : false;
}

IKIGAI::RESOURCES::FileSystem::FileTime IKIGAI::RESOURCES::FileSystem::lastWriteTime(const std::string& path) {
	auto pathOpt = getAbsolutePath(path);
	if (!pathOpt) {
		throw UTILS::EXEPTIONS::WrongPath(path.c_str());
	}
	return std::filesystem::last_write_time(*pathOpt);
}

uintmax_t IKIGAI::RESOURCES::FileSystem::fileSize(const std::string& path) {
	auto pathOpt = getAbsolutePath(path);
	if (!pathOpt) {
		throw UTILS::EXEPTIONS::WrongPath(path.c_str());
	}
	return std::filesystem::file_size(*pathOpt);
}

std::shared_ptr<IKIGAI::RESOURCES::File> IKIGAI::RESOURCES::FileSystem::getFile(const std::string& path, FileMode mode) {
	vfspp::IFilePtr file = mInternal->mVFS->OpenFile(path, static_cast<vfspp::IFile::FileMode>(mode));
	return std::make_shared<File>(std::make_unique<FileInternal>(file));
}

std::optional<std::string> IKIGAI::RESOURCES::FileSystem::getFilePath(const std::string& path) const {
    auto entry = mInternal->mVFS->GetEntryInfo(path);
    if (entry) return entry->VirtualPath();
	return std::nullopt;
}
