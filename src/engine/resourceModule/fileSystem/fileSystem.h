#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace IKIGAI::RESOURCES {
	class FileSystemInternal;
	class FileInternal;

	enum class FileMode : uint8_t {
		READ = 1,
		WRITE = 2,
		READ_WRITE = READ | WRITE,
		APPEND = 4,
		TRUNCATE = 8
	};

	class File {
		std::unique_ptr<FileInternal> mInternal;

		std::vector<uint8_t> read(size_t sz);
		void read(uint8_t* data, size_t sz);
		void write(const uint8_t* data, size_t sz);
	public:
		File(std::unique_ptr<FileInternal> internal);
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
		std::string readStr();
		template<typename T>
		void read(T& value) {
			read(reinterpret_cast<uint8_t*>(&value), sizeof(value));
		}

		template<typename T>
		void write(const T& value) {
			write(reinterpret_cast<const uint8_t*>(&value), sizeof(value));
		}
		template<>
		void write(const std::string& value) {
			write(reinterpret_cast<const uint8_t*>(value.data()), value.size());
		}
		template<>
		void write(const std::vector<uint8_t>& value) {
			write(value.data(), value.size());
		}
	};

	class FileSystem {
		std::unique_ptr<FileSystemInternal> mInternal;
	public:
		using FileTime = std::filesystem::file_time_type;

		FileSystem();
		~FileSystem();

		void addNativeFileSystem(const std::string& path, const std::string& pathInFs);
		void addZipFileSystem(const std::string& path, const std::string& pathInFs);
		void addMemoryFileSystem(const std::string& pathInFs);
		void addSdlFileSystem(const std::string& path, const std::string& pathInFs);
		void addAndroidAssetFileSystem(const std::string& path, const std::string& pathInFs, void* assetManager, void* androidApp);

		bool isValid(const std::string& path) const;
		bool isFileExist(const std::string& path) const;
		std::string getFileExtension(const std::string& path) const;
		std::string getFileName(const std::string& path) const;
		std::optional<std::string> getAbsolutePath(const std::string& path) const;
		bool isDir(const std::string& path) const;

		FileTime lastWriteTime(const std::string& path);
		uintmax_t fileSize(const std::string& path);

		struct DirectoryEntry {
			std::string path;
			std::string name;
			bool isDirectory = false;
		};

		std::shared_ptr<File> getFile(const std::string& path, FileMode mode = FileMode::READ_WRITE);
		std::optional<std::string> getFilePath(const std::string& path) const;
		std::vector<DirectoryEntry> listDirectory(const std::string& path) const;
	};
}
