#pragma once
#include "resourceModule/resourceRequest.h"
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>


namespace IKIGAI::RESOURCES {
namespace STORAGE {
	class UserStorage;
}
class FileSystemInternal;
class FileInternal;
class RangeFileBacking;

enum class FileMode : uint8_t { READ = 1, WRITE = 2, READ_WRITE = READ | WRITE, APPEND = 4, TRUNCATE = 8 };

enum class RangeFileErrorCode { NotFound, UnsupportedBacking, InvalidRange, InvalidArchive, IoError, ShortRead, Cancelled };

class RangeFileError : public std::runtime_error {
	RangeFileErrorCode mCode;

public:
	RangeFileError(RangeFileErrorCode code, const std::string& message);
	RangeFileErrorCode code() const noexcept;
};

// Keeps the resolved backing open. The caller must treat its contents as immutable.
class RangeFile {
	friend class FileSystem;
	std::shared_ptr<RangeFileBacking> mBacking;
	uint64_t mOffset;
	uint64_t mSize;

	RangeFile(std::shared_ptr<RangeFileBacking> backing, uint64_t offset, uint64_t size);

public:
	uint64_t size() const noexcept;
	// Reads exactly size bytes or throws RangeFileError. Concurrent reads have independent offsets.
	std::vector<uint8_t> read(uint64_t offset, std::size_t size, std::stop_token stop = {}) const;
};

class File {
	std::shared_ptr<FileInternal> mInternal;

	void read(uint8_t* data, size_t sz);
	void write(const uint8_t* data, size_t sz);

public:
	File(std::unique_ptr<FileInternal> internal);
	~File();
	File(const File&) = delete;
	File& operator=(const File&) = delete;
	size_t tell() const;
	void seek(size_t offset);
	bool isValid() const;
	std::string getFileExtension() const;
	std::string getFileName() const;
	std::string getAbsolutePath() const;
	bool isDir() const;
	bool isReadOnly() const;
	size_t getSize() const;
	bool isOpened() const;
	void open(FileMode mode) const;
	void close() const;

	std::vector<uint8_t> read();
	std::vector<uint8_t> readChunk(size_t maxBytes);
	// Retains this handle and its mount. The read advances its cursor, including any bytes consumed before cancellation.
	// Cursor operations on the same handle throw std::logic_error while a read owns it.
	COROUTINE::Task<std::vector<uint8_t>> readAllTask(std::shared_ptr<ResourceLoadControl> control = {});
	std::string readStr();
	template <typename T> void read(T& value) {
		read(reinterpret_cast<uint8_t*>(&value), sizeof(value));
	}

	template <typename T> void write(const T& value) {
		write(reinterpret_cast<const uint8_t*>(&value), sizeof(value));
	}
	void write(const std::string& value) {
		write(reinterpret_cast<const uint8_t*>(value.data()), value.size());
	}
	void write(const std::vector<uint8_t>& value) {
		write(value.data(), value.size());
	}
};

class FileSystem {
	std::unique_ptr<FileSystemInternal> mInternal;

public:
	using FileTime = std::filesystem::file_time_type;
	using MountId = uint64_t;
	struct MountInfo {
		MountId id;
		std::string source, alias;
		bool readOnly;
		std::vector<std::string> deletedPaths;
	};

	FileSystem();
	~FileSystem();

	void addNativeFileSystem(const std::string& path, const std::string& pathInFs);
	MountId mountNative(const std::string& path, const std::string& alias = "/", bool readOnly = false);
	MountId mountStorage(std::shared_ptr<STORAGE::UserStorage> storage, const std::string& prefix, const std::string& alias);
	MountId mountZip(const std::string& path, const std::string& alias = "/", const std::vector<std::string>& deletedPaths = { });
	bool unmount(MountId id);
	std::vector<MountInfo> mounts() const;
	uint64_t generation() const;
	// Pins the mount generation while publishing already prepared resource state.
	// An empty lock means the candidate was read from a different generation.
	std::unique_lock<std::recursive_mutex> lockGeneration(uint64_t expected) const;
	static std::string normalizePath(std::string path);
	void addZipFileSystem(const std::string& path, const std::string& pathInFs);
	void addMemoryFileSystem(const std::string& pathInFs);
	void addSdlFileSystem(const std::string& path, const std::string& pathInFs, bool readOnly = false);
	void addAndroidAssetFileSystem(const std::string& path, const std::string& pathInFs, void* assetManager, void* androidApp);

	bool isValid(const std::string& path) const;
	bool isFileExist(const std::string& path) const;
	std::string getFileExtension(const std::string& path) const;
	std::string getFileName(const std::string& path) const;
	std::optional<std::string> getAbsolutePath(const std::string& path) const;
	std::optional<std::string> getNativeWatchPath(const std::string& path) const;
	bool isDir(const std::string& path) const;

	FileTime lastWriteTime(const std::string& path);
	uintmax_t fileSize(const std::string& path);

	struct DirectoryEntry {
		std::string path;
		std::string name;
		bool isDirectory = false;
	};

	std::shared_ptr<File> getFile(const std::string& path, FileMode mode = FileMode::READ);
	// Resolves the winning mount and opens a native file or a ZIP_STORED member.
	std::shared_ptr<const RangeFile> openRange(const std::string& path) const;
	std::optional<std::string> getFilePath(const std::string& path) const;
	std::vector<DirectoryEntry> listDirectory(const std::string& path) const;
	// Tooling reads that discover files created/deleted after native mount
	// initialization, while retaining VFS mount precedence and packaged IO.
	std::shared_ptr<File> getLiveFile(const std::string& path);
	std::vector<DirectoryEntry> listLiveDirectory(const std::string& path) const;
	// Storage mounts include persistent commit in the returned completion.
	std::shared_future<void> writeTextAtomic(const std::string& path, const std::string& text);
	std::shared_future<void> writeAtomic(const std::string& path, std::span<const uint8_t> data);
};
}
