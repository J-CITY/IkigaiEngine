#include "fileSystem.h"
#include "nativeFile.h"
#include "resourceModule/serviceManager.h"
#include "storage.h"
#include "taskModule/taskSystem.h"
#include "userStorage.h"
#include "vfsCompatibility.h"
#include "vfspp/VFS.h"
#ifdef USE_SDL
#include "sdlFileSystem.h"
#endif
#ifdef OCULUS
#include "androidAssetFileSystem.h"
#include <android/asset_manager.h>
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace IKIGAI::RESOURCES {
RangeFileError::RangeFileError(RangeFileErrorCode code, const std::string& message)
	: std::runtime_error(message)
	, mCode(code) { }
RangeFileErrorCode RangeFileError::code() const noexcept {
	return mCode;
}

namespace {
	void CheckRangeStop(std::stop_token stop) {
		if (stop.stop_requested()) {
			throw RangeFileError(RangeFileErrorCode::Cancelled, "Range read cancelled");
		}
	}
}

class RangeFileBacking {
	std::mutex mReadMutex;

	virtual size_t readAt(uint64_t offset, std::span<uint8_t> output) = 0;

public:
	virtual ~RangeFileBacking() = default;
	void readExact(uint64_t offset, std::span<uint8_t> output, std::stop_token stop = {}) {
		std::lock_guard lock(mReadMutex);
		CheckRangeStop(stop);
		if (output.empty()) {
			return;
		}
		const auto count = readAt(offset, output);
		CheckRangeStop(stop);
		if (count != output.size()) {
			throw RangeFileError(RangeFileErrorCode::ShortRead, "Short range read");
		}
	}
};

RangeFile::RangeFile(std::shared_ptr<RangeFileBacking> backing, uint64_t offset, uint64_t size)
	: mBacking(std::move(backing))
	, mOffset(offset)
	, mSize(size) { }
uint64_t RangeFile::size() const noexcept {
	return mSize;
}
std::vector<uint8_t> RangeFile::read(uint64_t offset, std::size_t size, std::stop_token stop) const {
	CheckRangeStop(stop);
	if (offset > mSize || size > mSize - offset) {
		throw RangeFileError(RangeFileErrorCode::InvalidRange, "Read exceeds file range");
	}
	std::vector<uint8_t> bytes(size);
	mBacking->readExact(mOffset + offset, bytes, stop);
	return bytes;
}

namespace {
	class LooseRangeBacking final : public RangeFileBacking {
		std::ifstream mStream;
		uint64_t mSize;

		size_t readAt(uint64_t offset, std::span<uint8_t> output) override {
			if (output.size() > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max())) {
				throw RangeFileError(RangeFileErrorCode::InvalidRange, "Range exceeds stream read limit");
			}
			mStream.clear();
			mStream.seekg(static_cast<std::streamoff>(offset));
			if (!mStream) {
				throw RangeFileError(RangeFileErrorCode::IoError, "Cannot seek native backing");
			}
			mStream.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(output.size()));
			if (mStream.bad()) {
				throw RangeFileError(RangeFileErrorCode::IoError, "Cannot read native backing");
			}
			return static_cast<size_t>(mStream.gcount());
		}

	public:
		explicit LooseRangeBacking(const std::filesystem::path& path)
			: mStream(path, std::ios::binary | std::ios::ate)
			, mSize(0) {
			const auto end = mStream.tellg();
			if (end < 0) {
				throw RangeFileError(RangeFileErrorCode::IoError, "Cannot open native backing");
			}
			mSize = static_cast<uint64_t>(end);
		}
		uint64_t size() const {
			return mSize;
		}
	};

	class ZipRangeBacking final : public RangeFileBacking {
		mz_zip_archive mArchive {};
		std::unordered_map<std::string, mz_uint> mMembers;
		std::vector<uint64_t> mLocalOffsets;

		size_t readAt(uint64_t offset, std::span<uint8_t> output) override {
			return mArchive.m_pRead(mArchive.m_pIO_opaque, offset, output.data(), output.size());
		}

		static void validateExtra(std::span<const uint8_t> extra, const std::string& path) {
			constexpr size_t fieldHeaderSize = 4;
			constexpr uint16_t zip64Field = 1;
			while (!extra.empty()) {
				if (extra.size() < fieldHeaderSize) {
					throw RangeFileError(RangeFileErrorCode::InvalidArchive, "Truncated ZIP extra field: " + path);
				}
				const auto fieldSize = MZ_READ_LE16(extra.data() + 2);
				if (fieldSize > extra.size() - fieldHeaderSize) {
					throw RangeFileError(RangeFileErrorCode::InvalidArchive, "Truncated ZIP extra field: " + path);
				}
				if (MZ_READ_LE16(extra.data()) == zip64Field) {
					throw RangeFileError(RangeFileErrorCode::UnsupportedBacking, "ZIP64 extra field is unsupported: " + path);
				}
				extra = extra.subspan(fieldHeaderSize + fieldSize);
			}
		}

	public:
		struct Extent {
			uint64_t offset;
			uint64_t size;
		};

		explicit ZipRangeBacking(const std::string& path) {
			if (!mz_zip_reader_init_file(&mArchive, path.c_str(), 0)) {
				throw RangeFileError(RangeFileErrorCode::InvalidArchive, "Invalid ZIP: " + path);
			}
		}
		~ZipRangeBacking() override {
			mz_zip_reader_end(&mArchive);
		}
		mz_zip_archive& archive() {
			return mArchive;
		}
		void addMember(const std::string& virtualPath, const mz_zip_archive_file_stat& entry) {
			mMembers.insert_or_assign(virtualPath, entry.m_file_index);
			mLocalOffsets.push_back(entry.m_local_header_ofs);
		}
		void finishIndex() {
			std::sort(mLocalOffsets.begin(), mLocalOffsets.end());
		}

		Extent storedExtent(const std::string& path) {
			const auto found = mMembers.find(path);
			mz_zip_archive_file_stat entry {};
			if (found == mMembers.end() || !mz_zip_reader_file_stat(&mArchive, found->second, &entry)) {
				throw RangeFileError(RangeFileErrorCode::InvalidArchive, "Missing ZIP entry metadata: " + path);
			}
			constexpr uint32_t zip32Limit = std::numeric_limits<uint32_t>::max();
			constexpr uint16_t utf8Flag = 1u << 11;
			if (mArchive.m_archive_size >= zip32Limit || entry.m_version_needed >= 45 || entry.m_comp_size >= zip32Limit
				|| entry.m_uncomp_size >= zip32Limit || entry.m_local_header_ofs >= zip32Limit || entry.m_method != 0
				|| (entry.m_bit_flag & ~utf8Flag) != 0) {
				throw RangeFileError(RangeFileErrorCode::UnsupportedBacking, "Range reads require plain ZIP32 stored entries: " + path);
			}

			const auto current = std::lower_bound(mLocalOffsets.begin(), mLocalOffsets.end(), entry.m_local_header_ofs);
			const auto next = std::next(current);
			const auto boundary = next == mLocalOffsets.end() ? mArchive.m_central_directory_file_ofs : *next;
			std::array<uint8_t, MZ_ZIP_LOCAL_DIR_HEADER_SIZE> header {};
			const auto offset = entry.m_local_header_ofs;
			if (boundary > mArchive.m_central_directory_file_ofs || offset > boundary || header.size() > boundary - offset) {
				throw RangeFileError(RangeFileErrorCode::InvalidArchive, "ZIP local header lies outside its extent: " + path);
			}
			readExact(offset, header);
			if (MZ_READ_LE16(header.data() + MZ_ZIP_LDH_VERSION_NEEDED_OFS) >= 45) {
				throw RangeFileError(RangeFileErrorCode::UnsupportedBacking, "ZIP64 local header is unsupported: " + path);
			}
			const auto nameSize = MZ_READ_LE16(header.data() + MZ_ZIP_LDH_FILENAME_LEN_OFS);
			const auto extraSize = MZ_READ_LE16(header.data() + MZ_ZIP_LDH_EXTRA_LEN_OFS);
			const auto nameOffset = offset + header.size();
			if (MZ_READ_LE32(header.data()) != MZ_ZIP_LOCAL_DIR_HEADER_SIG || MZ_READ_LE16(header.data() + MZ_ZIP_LDH_METHOD_OFS) != entry.m_method
				|| MZ_READ_LE16(header.data() + MZ_ZIP_LDH_BIT_FLAG_OFS) != entry.m_bit_flag
				|| MZ_READ_LE32(header.data() + MZ_ZIP_LDH_CRC32_OFS) != entry.m_crc32
				|| MZ_READ_LE32(header.data() + MZ_ZIP_LDH_COMPRESSED_SIZE_OFS) != entry.m_comp_size
				|| MZ_READ_LE32(header.data() + MZ_ZIP_LDH_DECOMPRESSED_SIZE_OFS) != entry.m_uncomp_size || entry.m_comp_size != entry.m_uncomp_size
				|| nameSize != std::strlen(entry.m_filename) || static_cast<uint32_t>(nameSize) + extraSize > boundary - nameOffset) {
				throw RangeFileError(RangeFileErrorCode::InvalidArchive, "Invalid or inconsistent ZIP local header: " + path);
			}
			const auto* central = mz_zip_reader_get_cdh(&mArchive, found->second);
			const auto centralNameSize = MZ_READ_LE16(central + MZ_ZIP_CDH_FILENAME_LEN_OFS);
			if (centralNameSize != nameSize) {
				throw RangeFileError(RangeFileErrorCode::InvalidArchive, "Invalid ZIP central filename: " + path);
			}
			validateExtra({ central + MZ_ZIP_CENTRAL_DIR_HEADER_SIZE + centralNameSize, MZ_READ_LE16(central + MZ_ZIP_CDH_EXTRA_LEN_OFS) }, path);
			const auto dataOffset = nameOffset + nameSize + extraSize;
			if (entry.m_uncomp_size > boundary - dataOffset) {
				throw RangeFileError(RangeFileErrorCode::InvalidArchive, "ZIP payload extends past its boundary: " + path);
			}
			std::vector<uint8_t> localNameAndExtra(static_cast<size_t>(nameSize) + extraSize);
			readExact(nameOffset, localNameAndExtra);
			if (std::memcmp(localNameAndExtra.data(), entry.m_filename, nameSize) != 0) {
				throw RangeFileError(RangeFileErrorCode::InvalidArchive, "ZIP local and central filenames differ: " + path);
			}
			validateExtra(std::span(localNameAndExtra).subspan(nameSize), path);
			return { dataOffset, entry.m_uncomp_size };
		}
	};

	bool Contains(const std::string& base, const std::string& path) {
		return base == "/" || path == base || path.starts_with(base + "/");
	}
	bool Reserved(const std::string& path) {
		return Contains("/user", path) || Contains("/cache", path);
	}
	struct Mount {
		FileSystem::MountInfo info;
		vfspp::IFileSystemPtr fs;
		std::shared_ptr<STORAGE::UserStorage> storage;
		std::string prefix;
		std::shared_ptr<ZipRangeBacking> zipRange;
		bool memory = false;
		// Ordinary vfspp ZIP handles share one archive cursor across the mount.
		std::mutex zipReadMutex;
		// vfspp also appends to a per-entry handle list in OpenFile.
		std::mutex zipOpenMutex;
		// Memory mounts share both the entry table and each file's mutable bytes.
		mutable std::mutex memoryMutex;
		[[nodiscard]] std::unique_lock<std::mutex> lockMemory() const {
			return memory && !storage ? std::unique_lock { memoryMutex } : std::unique_lock<std::mutex> {};
		}
		std::string storagePath(const std::string& path) const {
			const auto relative = STORAGE::Utf8(std::filesystem::u8path(path).lexically_relative(std::filesystem::u8path(info.alias)));
			return relative == "." ? prefix : prefix + "/" + relative;
		}
	};
	std::optional<vfspp::EntryInfo> Entry(const Mount& mount, const std::string& path) {
		if (path == mount.info.alias) {
			return vfspp::EntryInfo(mount.info.alias, mount.info.source, path, vfspp::EntryType::Directory);
		}
		if (mount.storage) {
			const auto key = mount.storagePath(path);
			if (!mount.storage->exists(key)) {
				return {};
			}
			return vfspp::EntryInfo(
				mount.info.alias, "", path, mount.storage->isDirectory(key) ? vfspp::EntryType::Directory : vfspp::EntryType::File);
		}
		if (std::dynamic_pointer_cast<vfspp::NativeFileSystem>(mount.fs)) {
			vfspp::EntryInfo entry(mount.info.alias, mount.info.source, path);
			std::error_code ec;
			const auto relative = std::filesystem::path(path).lexically_relative(mount.info.alias).generic_string();
			const auto status = std::filesystem::status(STORAGE::Child(std::filesystem::u8path(mount.info.source), relative), ec);
			if (ec || (!std::filesystem::is_regular_file(status) && !std::filesystem::is_directory(status))) {
				return {};
			}
			return vfspp::EntryInfo(mount.info.alias, mount.info.source, path,
				std::filesystem::is_directory(status) ? vfspp::EntryType::Directory : vfspp::EntryType::File);
		}
		const auto memoryLock = mount.lockMemory();
		return mount.fs->GetEntryInfo(path);
	}
}
class FileSystemInternal {
public:
	std::vector<std::shared_ptr<Mount>> mounts;
	mutable std::recursive_mutex mutex;
	uint64_t nextId = 1;
	std::atomic<uint64_t> generation { 0 };
	FileSystem::MountId add(vfspp::IFileSystemPtr fs, std::vector<std::string> deleted = {}, std::shared_ptr<ZipRangeBacking> zipRange = {}) {
		if (!fs->Initialize()) {
			throw std::runtime_error("Cannot initialize filesystem: " + fs->BasePath());
		}
		std::lock_guard lock(mutex);
		auto alias = FileSystem::normalizePath(fs->VirtualPath());
		auto record = std::make_shared<Mount>(FileSystem::MountInfo { nextId++, fs->BasePath(), alias, fs->IsReadOnly(), std::move(deleted) }, fs,
			std::shared_ptr<STORAGE::UserStorage> {}, std::string {}, std::move(zipRange),
			dynamic_cast<vfspp::MemoryFileSystem*>(fs.get()) != nullptr);
		mounts.push_back(record);
		++generation;
		return record->info.id;
	}
	std::vector<std::shared_ptr<Mount>> ordered(const std::string& path) const {
		std::lock_guard lock(mutex);
		std::vector<std::shared_ptr<Mount>> result;
		for (auto it = mounts.rbegin(); it != mounts.rend(); ++it)
			if (Contains((*it)->info.alias, path))
				result.push_back(*it);
		std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a->info.alias.size() > b->info.alias.size(); });
		return result;
	}
	std::shared_ptr<Mount> resolve(const std::string& path) const {
		for (const auto& mount : ordered(path)) {
			if (Reserved(path) && mount->info.alias == "/")
				return { };
			if (Entry(*mount, path))
				return mount;
			for (const auto& deleted : mount->info.deletedPaths)
				if (Contains(deleted, path))
					return { };
			auto parent = std::filesystem::path(path).parent_path();
			while (!parent.empty() && Contains(mount->info.alias, parent.generic_string())) {
				const auto entry = Entry(*mount, parent.generic_string());
				if (entry && !entry->IsDirectory())
					return { };
				if (parent == parent.root_path())
					break;
				parent = parent.parent_path();
			}
			if (Reserved(path))
				return { };
		}
		return { };
	}
};
class FileInternal {
public:
	class CursorLease {
	public:
		explicit CursorLease(std::atomic_flag& inUse)
			: mInUse(inUse) {
			if (mInUse.test_and_set(std::memory_order_acquire)) {
				throw std::logic_error("File cursor is already in use");
			}
		}
		~CursorLease() {
			mInUse.clear(std::memory_order_release);
		}
		CursorLease(const CursorLease&) = delete;
		CursorLease& operator=(const CursorLease&) = delete;

	private:
		std::atomic_flag& mInUse;
	};

	// vfspp ZipFile holds a weak archive reference. Retain the owning mount.
	std::shared_ptr<Mount> owner;
	vfspp::IFilePtr file;
	bool closed = false;
	FileInternal(vfspp::IFilePtr value, std::shared_ptr<Mount> mount)
		: owner(std::move(mount))
		, file(std::move(value)) { }
	[[nodiscard]] CursorLease acquireCursor() {
		return CursorLease(cursorInUse);
	}
	bool isOpened() const {
		return !closed && file && file->IsOpened();
	}
	bool isReadOnly() const {
		return !file || owner->info.readOnly || bool(owner->storage);
	}
	size_t size() const {
		const auto memoryLock = lockMemory();
		return file ? file->Size() : 0;
	}
	size_t tell() const {
		return file ? file->Tell() : 0;
	}
	size_t remaining() const {
		const auto total = size();
		return total - std::min(total, tell());
	}
	std::vector<uint8_t> read(size_t size) {
		const auto backingLock = lockRead();
		std::vector<uint8_t> result;
		if (isOpened()) {
			result.resize(static_cast<size_t>(file->Read(result, size)));
		}
		return result;
	}
	size_t read(std::span<uint8_t> output) {
		const auto backingLock = lockRead();
		return isOpened() ? file->Read(output) : 0;
	}
	[[nodiscard]] std::unique_lock<std::mutex> lockMemory() const {
		return owner ? owner->lockMemory() : std::unique_lock<std::mutex> {};
	}

private:
	std::atomic_flag cursorInUse = {};

	[[nodiscard]] std::unique_lock<std::mutex> lockRead() const {
		return owner && owner->zipRange ? std::unique_lock { owner->zipReadMutex } : lockMemory();
	}
};
std::string FileSystem::normalizePath(std::string path) {
	std::replace(path.begin(), path.end(), '\\', '/');
	if (path.find(':') != std::string::npos || path.find('\0') != std::string::npos || path.starts_with("//")) {
		throw std::runtime_error("Expected a virtual path: " + path);
	}
	for (const auto& part : std::filesystem::u8path(path)) {
		if (part == "..") {
			throw std::runtime_error("Parent traversal in virtual path: " + path);
		}
	}
	if (path.empty() || path.front() != '/') {
		path.insert(path.begin(), '/');
	}
	path = STORAGE::Utf8(std::filesystem::u8path(path).lexically_normal());
	while (path.size() > 1 && path.back() == '/') {
		path.pop_back();
	}
	return path;
}
File::File(std::unique_ptr<FileInternal> internal)
	: mInternal(std::move(internal)) { }
File::~File() = default;
bool File::isValid() const {
	return isOpened();
}
bool File::isOpened() const {
	const auto cursor = mInternal->acquireCursor();
	return mInternal->isOpened();
}
bool File::isReadOnly() const {
	const auto cursor = mInternal->acquireCursor();
	return mInternal->isReadOnly();
}
bool File::isDir() const {
	const auto cursor = mInternal->acquireCursor();
	return mInternal->file && mInternal->file->GetEntryInfo().IsDirectory();
}
size_t File::getSize() const {
	const auto cursor = mInternal->acquireCursor();
	return mInternal->size();
}
size_t File::tell() const {
	const auto cursor = mInternal->acquireCursor();
	return mInternal->tell();
}
void File::seek(size_t offset) {
	const auto cursor = mInternal->acquireCursor();
	const auto memoryLock = mInternal->lockMemory();
	if (!mInternal->isOpened() || mInternal->file->Seek(offset, vfspp::IFile::Origin::Begin) != offset) {
		throw std::runtime_error("Cannot seek file");
	}
}
std::string File::getFileExtension() const {
	const auto cursor = mInternal->acquireCursor();
	return mInternal->file ? mInternal->file->GetEntryInfo().Extension() : "";
}
std::string File::getFileName() const {
	const auto cursor = mInternal->acquireCursor();
	return mInternal->file ? mInternal->file->GetEntryInfo().Filename() : "";
}
std::string File::getAbsolutePath() const {
	const auto cursor = mInternal->acquireCursor();
	return mInternal->file ? mInternal->file->GetEntryInfo().NativePath() : "";
}
void File::open(FileMode mode) const {
	const auto cursor = mInternal->acquireCursor();
	const auto memoryLock = mInternal->lockMemory();
	if (!mInternal->file || (mInternal->isReadOnly() && (static_cast<uint8_t>(mode) & 2))
		|| !mInternal->file->Open(static_cast<vfspp::IFile::FileMode>(mode))) {
		throw std::runtime_error("Cannot open file");
	}
	mInternal->closed = false;
}
void File::close() const {
	const auto cursor = mInternal->acquireCursor();
	if (mInternal->file) {
		mInternal->file->Close();
	}
	mInternal->closed = true;
}
std::vector<uint8_t> File::read() {
	const auto cursor = mInternal->acquireCursor();
	return mInternal->read(mInternal->remaining());
}
std::vector<uint8_t> File::readChunk(size_t size) {
	const auto cursor = mInternal->acquireCursor();
	if (!mInternal->isOpened()) {
		throw std::runtime_error("File is not open");
	}
	return mInternal->read(size);
}
std::string File::readStr() {
	const auto bytes = read();
	return { bytes.begin(), bytes.end() };
}
void File::read(uint8_t* data, size_t size) {
	const auto cursor = mInternal->acquireCursor();
	if (!mInternal->isOpened() || mInternal->read(std::span<uint8_t>(data, size)) != size) {
		throw std::runtime_error("Short file read");
	}
}
void File::write(const uint8_t* data, size_t size) {
	const auto cursor = mInternal->acquireCursor();
	const auto memoryLock = mInternal->lockMemory();
	if (!mInternal->isOpened() || mInternal->isReadOnly() || mInternal->file->Write(std::span<const uint8_t>(data, size)) != size) {
		throw std::runtime_error("Short file write");
	}
}
namespace {
	struct FileReadState {
		std::shared_ptr<FileInternal> file;
		FileInternal::CursorLease cursor;
		std::vector<uint8_t> bytes;
		size_t offset = 0;

		explicit FileReadState(std::shared_ptr<FileInternal> handle)
			: file(std::move(handle))
			, cursor(file->acquireCursor()) { }

		void prepare() {
			if (!file->isOpened()) {
				throw std::runtime_error("File is not open");
			}
			bytes.resize(file->remaining());
		}

		void readNext() {
			constexpr size_t chunkSize = 64 * 1024;
			const auto count = file->read(std::span<uint8_t>(bytes).subspan(offset, std::min(chunkSize, bytes.size() - offset)));
			if (count == 0) {
				throw std::runtime_error("Unexpected EOF: " + file->file->GetEntryInfo().Filename());
			}
			offset += count;
		}
	};

	COROUTINE::Task<std::vector<uint8_t>> readFileTask(std::shared_ptr<FileInternal> file, std::shared_ptr<ResourceLoadControl> control) {
		const auto check = [&] {
			if (control) {
				control->check();
			}
		};
		check();
		auto state = std::make_shared<FileReadState>(std::move(file));
#ifdef __EMSCRIPTEN__
		// The WebAssembly Core uses a single-threaded scheduler.
		state->prepare();
		while (state->offset < state->bytes.size()) {
			state->readNext();
			co_await COROUTINE::NextFrame();
			check();
		}
#else
		if (!ServiceManager::Check<TASK::TaskSystem>()) {
			throw std::runtime_error("File read requires the I/O executor");
		}
		auto& tasks = ServiceManager::Get<TASK::TaskSystem>();
		struct CancelOnExit {
			std::stop_source source;
			~CancelOnExit() {
				source.request_stop();
			}
		} cancellation;
		const auto work = [state, stop = cancellation.source.get_token()](std::stop_token shutdown) {
			if (stop.stop_requested() || shutdown.stop_requested()) {
				return;
			}
			state->prepare();
			while (state->offset < state->bytes.size() && !stop.stop_requested() && !shutdown.stop_requested()) {
				state->readNext();
			}
		};
		auto ticket = tasks.dispatchIo("Read file", work);
		while (!ticket.accepted() && ticket.get().Rejection == TASK::BackgroundRejection::QueueFull) {
			co_await COROUTINE::NextFrame();
			check();
			ticket = tasks.dispatchIo("Read file", work);
		}
		while (!ticket.ready()) {
			co_await COROUTINE::NextFrame();
			check();
		}
		check();
		const auto result = ticket.get();
		if (result.Exception) {
			std::rethrow_exception(result.Exception);
		}
		if (result.Outcome == TASK::BackgroundOutcome::Cancelled || result.Rejection == TASK::BackgroundRejection::Stopping) {
			throw ResourceLoadCancelled();
		}
		if (result.Outcome != TASK::BackgroundOutcome::Succeeded) {
			throw std::runtime_error(std::string("Cannot schedule file read: ") + TASK::backgroundRejectionMessage(result.Rejection));
		}
#endif
		co_return std::move(state->bytes);
	}
}
COROUTINE::Task<std::vector<uint8_t>> File::readAllTask(std::shared_ptr<ResourceLoadControl> control) {
	return readFileTask(mInternal, std::move(control));
}
FileSystem::FileSystem()
	: mInternal(std::make_unique<FileSystemInternal>()) { }
FileSystem::~FileSystem() = default;
FileSystem::MountId FileSystem::mountNative(const std::string& path, const std::string& alias, bool readOnly) {
	return mInternal->add(std::make_shared<vfspp::NativeFileSystem>(normalizePath(alias), path, readOnly));
}
FileSystem::MountId FileSystem::mountStorage(std::shared_ptr<STORAGE::UserStorage> storage, const std::string& prefix, const std::string& alias) {
	STORAGE::Child(storage->root(), prefix);
	if (!storage->isDirectory(prefix))
		throw std::runtime_error("Storage mount directory is missing");
	std::lock_guard lock(mInternal->mutex);
	const auto id = mInternal->add(std::make_shared<vfspp::MemoryFileSystem>(normalizePath(alias)));
	auto& mount = mInternal->mounts.back();
	mount->storage = std::move(storage);
	mount->prefix = prefix;
	mount->info.source = "storage:" + prefix;
	return id;
}
FileSystem::MountId FileSystem::mountZip(const std::string& path, const std::string& alias, const std::vector<std::string>& deletedPaths) {
	const auto normalized = normalizePath(alias);
	if (Reserved(normalized))
		throw std::runtime_error("Archive cannot mount over user storage");
	std::vector<std::string> deleted;
	for (const auto& item : deletedPaths) {
		auto key = normalizePath(item);
		if (key == "/" || Reserved(key) || !Contains(normalized, key))
			throw std::runtime_error("Invalid deletion: " + item);
		deleted.push_back(key);
	}
	auto backing = std::make_shared<ZipRangeBacking>(path);
	auto& zip = backing->archive();
	std::set<std::string> names;
	uint64_t total = 0;
	const auto count = mz_zip_reader_get_num_files(&zip);
	if (count > 100000)
		throw std::runtime_error("Too many ZIP entries");
	for (mz_uint i = 0; i < count; ++i) {
		mz_zip_archive_file_stat stat { };
		if (!mz_zip_reader_file_stat(&zip, i, &stat))
			throw std::runtime_error("Invalid ZIP entry");
		const std::string raw(stat.m_filename);
		if (raw.empty() || raw.front() == '/' || raw.find('\\') != std::string::npos)
			throw std::runtime_error("Invalid ZIP path: " + raw);
		auto key = normalizePath((normalized == "/" ? "" : normalized) + "/" + raw);
		if (Reserved(key))
			throw std::runtime_error("ZIP contains reserved path");
		backing->addMember(vfspp::EntryInfo(normalized, "", raw).VirtualPath(), stat);
		std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (!names.insert(key).second)
			throw std::runtime_error("Duplicate ZIP path: " + raw);
		if (stat.m_uncomp_size > (uint64_t { 1 } << 32) || total > (uint64_t { 1 } << 35) - stat.m_uncomp_size)
			throw std::runtime_error("ZIP size limit exceeded");
		total += stat.m_uncomp_size;
	}
	backing->finishIndex();
	return mInternal->add(std::make_shared<vfspp::ZipFileSystem>(normalized, path), std::move(deleted), std::move(backing));
}
void FileSystem::addNativeFileSystem(const std::string& path, const std::string& alias) {
	mountNative(path, alias);
}
void FileSystem::addZipFileSystem(const std::string& path, const std::string& alias) {
	mountZip(path, alias);
}
void FileSystem::addMemoryFileSystem(const std::string& alias) {
	mInternal->add(std::make_shared<vfspp::MemoryFileSystem>(normalizePath(alias)));
}
void FileSystem::addSdlFileSystem(const std::string& path, const std::string& alias, bool readOnly) {
#ifdef USE_SDL
	mInternal->add(std::make_shared<SdlFileSystem>(normalizePath(alias), path, readOnly));
#else
	throw std::runtime_error("SDL filesystem unavailable");
#endif
}
void FileSystem::addAndroidAssetFileSystem(const std::string& path, const std::string& alias, void* assets, void* app) {
#ifdef OCULUS
	mInternal->add(std::make_shared<AndroidAssetFileSystem>(normalizePath(alias), path, static_cast<AAssetManager*>(assets), app));
#else
	throw std::runtime_error("Android filesystem unavailable");
#endif
}
bool FileSystem::unmount(MountId id) {
	std::lock_guard lock(mInternal->mutex);
	const auto size = mInternal->mounts.size();
	std::erase_if(mInternal->mounts, [id](const auto& mount) { return mount->info.id == id; });
	if (mInternal->mounts.size() == size)
		return false;
	++mInternal->generation;
	return true;
}
std::vector<FileSystem::MountInfo> FileSystem::mounts() const {
	std::lock_guard lock(mInternal->mutex);
	std::vector<MountInfo> result;
	for (const auto& mount : mInternal->mounts)
		result.push_back(mount->info);
	return result;
}
uint64_t FileSystem::generation() const {
	return mInternal->generation;
}
std::unique_lock<std::recursive_mutex> FileSystem::lockGeneration(uint64_t expected) const {
	std::unique_lock lock(mInternal->mutex);
	if (mInternal->generation != expected) {
		return {};
	}
	return lock;
}
bool FileSystem::isValid(const std::string& path) const {
	return bool(mInternal->resolve(normalizePath(path)));
}
bool FileSystem::isFileExist(const std::string& path) const {
	const auto key = normalizePath(path);
	const auto mount = mInternal->resolve(key);
	return mount && !Entry(*mount, key)->IsDirectory();
}
bool FileSystem::isDir(const std::string& path) const {
	const auto key = normalizePath(path);
	const auto mount = mInternal->resolve(key);
	return mount && Entry(*mount, key)->IsDirectory();
}
std::string FileSystem::getFileExtension(const std::string& path) const {
	return STORAGE::Utf8(std::filesystem::u8path(normalizePath(path)).extension());
}
std::string FileSystem::getFileName(const std::string& path) const {
	return STORAGE::Utf8(std::filesystem::u8path(normalizePath(path)).filename());
}
std::optional<std::string> FileSystem::getAbsolutePath(const std::string& path) const {
	const auto key = normalizePath(path);
	const auto mount = mInternal->resolve(key);
	if (mount && std::dynamic_pointer_cast<vfspp::NativeFileSystem>(mount->fs))
		return Entry(*mount, key)->NativePath();
	return { };
}
std::optional<std::string> FileSystem::getNativeWatchPath(const std::string& path) const {
	const auto key = normalizePath(path);
	if (auto native = getAbsolutePath(key); native || isValid(key)) {
		return native;
	}
	const auto target = std::filesystem::u8path(key);
	for (auto parent = target.parent_path(); !parent.empty(); parent = parent.parent_path()) {
		const auto ancestor = STORAGE::Utf8(parent);
		if (isFileExist(ancestor)) {
			return {};
		}
		if (const auto native = getAbsolutePath(ancestor)) {
			return STORAGE::Utf8(STORAGE::Child(std::filesystem::u8path(*native), STORAGE::Utf8(target.lexically_relative(parent))));
		}
		if (parent == parent.root_path()) {
			break;
		}
	}
	return {};
}
std::optional<std::string> FileSystem::getFilePath(const std::string& path) const {
	const auto key = normalizePath(path);
	return isValid(key) ? std::optional(key) : std::nullopt;
}
FileSystem::FileTime FileSystem::lastWriteTime(const std::string& path) {
	const auto native = getAbsolutePath(path);
	std::error_code ec;
	return native ? std::filesystem::last_write_time(std::filesystem::u8path(*native), ec) : FileTime { };
}
uintmax_t FileSystem::fileSize(const std::string& path) {
	if (const auto native = getAbsolutePath(path)) {
		std::error_code error;
		const auto size = std::filesystem::file_size(std::filesystem::u8path(*native), error);
		if (!error)
			return size;
	}
	return getFile(path)->getSize();
}
std::shared_ptr<const RangeFile> FileSystem::openRange(const std::string& path) const {
	const auto key = normalizePath(path);
	const auto mount = mInternal->resolve(key);
	if (!mount) {
		throw RangeFileError(RangeFileErrorCode::NotFound, "Range file not found: " + key);
	}
	const auto entry = Entry(*mount, key);
	if (!entry || entry->IsDirectory()) {
		throw RangeFileError(RangeFileErrorCode::NotFound, "Range file not found: " + key);
	}
	if (mount->zipRange) {
		const auto extent = mount->zipRange->storedExtent(entry->VirtualPath());
		return std::shared_ptr<const RangeFile>(new RangeFile(mount->zipRange, extent.offset, extent.size));
	}
	if (std::dynamic_pointer_cast<vfspp::NativeFileSystem>(mount->fs)) {
		const auto relative = std::filesystem::path(key).lexically_relative(mount->info.alias).generic_string();
		const auto native = STORAGE::Child(std::filesystem::u8path(mount->info.source), relative);
		auto backing = std::make_shared<LooseRangeBacking>(native);
		const auto size = backing->size();
		return std::shared_ptr<const RangeFile>(new RangeFile(std::move(backing), 0, size));
	}
	throw RangeFileError(RangeFileErrorCode::UnsupportedBacking, "Mount does not support range reads: " + key);
}
std::shared_ptr<File> FileSystem::getFile(const std::string& path, FileMode mode) {
	const auto key = normalizePath(path);
	auto mount = mInternal->resolve(key);
	const bool writing = (static_cast<uint8_t>(mode) & 2) != 0;
	if (!mount && writing) {
		const auto candidates = mInternal->ordered(key);
		if (!candidates.empty())
			mount = candidates.front();
	}
	vfspp::IFilePtr file;
	if (mount && !isDir(key) && (!Reserved(key) || mount->info.alias != "/") && (!writing || !mount->info.readOnly)) {
		if (mount->storage) {
			// Mutable streaming handles have no portable async commit contract.
			// Use writeAtomic/profile.writeFile for writes to this mount.
			if (!writing && mount->storage->exists(mount->storagePath(key))) {
				auto object = std::make_shared<vfspp::MemoryFileObject>();
				*object->GetWritableData() = mount->storage->read(mount->storagePath(key));
				auto handle = std::make_shared<vfspp::MemoryFile>(vfspp::EntryInfo(mount->info.alias, "", key), object);
				if (handle->Open(vfspp::IFile::FileMode::Read))
					file = handle;
			}
		}
		else if (std::dynamic_pointer_cast<vfspp::NativeFileSystem>(mount->fs)) {
			const auto relative = std::filesystem::path(key).lexically_relative(mount->info.alias).generic_string();
			STORAGE::Child(std::filesystem::u8path(mount->info.source), relative);
			auto handle = std::make_shared<NativeDiskFile>(vfspp::EntryInfo(mount->info.alias, mount->info.source, key));
			if (handle->Open(static_cast<vfspp::IFile::FileMode>(mode)))
				file = handle;
		}
		else {
			const auto openLock = mount->zipRange ? std::unique_lock { mount->zipOpenMutex } : mount->lockMemory();
			file = mount->fs->OpenFile(key, static_cast<vfspp::IFile::FileMode>(mode));
		}
	}
	return std::make_shared<File>(std::make_unique<FileInternal>(std::move(file), std::move(mount)));
}
std::shared_ptr<File> FileSystem::getLiveFile(const std::string& path) {
	auto file = getFile(path);
	return file->isValid() ? file : nullptr;
}
std::vector<FileSystem::DirectoryEntry> FileSystem::listDirectory(const std::string& path) const {
	const auto key = normalizePath(path);
	if (!isDir(key))
		return { };
	std::set<std::string> candidates;
	for (const auto& mount : mInternal->ordered(key)) {
		if (mount->storage) {
			if (!mount->storage->isDirectory(mount->storagePath(key)))
				continue;
			for (const auto& entry : mount->storage->list(mount->storagePath(key)))
				candidates.insert(normalizePath(key + "/" + entry.name));
		}
		else if (std::dynamic_pointer_cast<vfspp::NativeFileSystem>(mount->fs)) {
			const vfspp::EntryInfo entry(mount->info.alias, mount->info.source, key);
			std::error_code ec;
			std::filesystem::directory_iterator end;
			for (std::filesystem::directory_iterator it(std::filesystem::u8path(entry.NativePath()), ec); !ec && it != end; it.increment(ec))
				candidates.insert(normalizePath((key == "/" ? "" : key) + "/" + it->path().filename().generic_string()));
		}
		else {
			const auto memoryLock = mount->lockMemory();
			for (const auto& entry : mount->fs->GetEntriesList(false)) {
				const auto candidate = normalizePath(entry.VirtualPath());
				if (std::filesystem::path(candidate).parent_path().generic_string() == key) {
					candidates.insert(candidate);
				}
			}
		}
	}
	std::vector<DirectoryEntry> result;
	for (const auto& candidate : candidates)
		if (auto mount = mInternal->resolve(candidate))
			result.push_back({ candidate, getFileName(candidate), Entry(*mount, candidate)->IsDirectory() });
	return result;
}
std::vector<FileSystem::DirectoryEntry> FileSystem::listLiveDirectory(const std::string& path) const {
	return listDirectory(path);
}
std::shared_future<void> FileSystem::writeAtomic(const std::string& path, std::span<const uint8_t> data) {
	const auto key = normalizePath(path);
	const auto candidates = mInternal->ordered(key);
	if (isDir(key))
		throw std::runtime_error("Cannot replace a VFS directory with a file");
	auto mount = mInternal->resolve(key);
	if (!mount && !candidates.empty())
		mount = candidates.front();
	if (mount && mount->storage) {
		mount->storage->writeAtomic(mount->storagePath(key), data);
		return mount->storage->commit();
	}
	if (!mount || (Reserved(key) && mount->info.alias == "/") || mount->info.readOnly
		|| !std::dynamic_pointer_cast<vfspp::NativeFileSystem>(mount->fs))
		throw std::runtime_error("No writable native mount: " + path);
	const vfspp::EntryInfo entry(mount->info.alias, mount->info.source, key);
	STORAGE::WriteAtomic(std::filesystem::u8path(entry.NativePath()), data);
	return STORAGE::ReadyCommit();
}
std::shared_future<void> FileSystem::writeTextAtomic(const std::string& path, const std::string& text) {
	const auto value = text + "\n";
	return writeAtomic(path, { reinterpret_cast<const uint8_t*>(value.data()), value.size() });
}
}
