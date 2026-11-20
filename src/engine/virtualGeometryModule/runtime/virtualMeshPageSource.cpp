#include "virtualMeshPageSource.h"
#include <utility>
namespace IKIGAI::VG {
namespace {
	constexpr uint64_t MaxResidentMetadataBytes = 256ull * 1024ull * 1024ull;

	std::vector<uint8_t> ReadExact(const VirtualMeshRangeReader& reader, uint64_t offset, std::size_t size, std::stop_token stop) {
		if (stop.stop_requested()) {
			throw VirtualMeshRangeError("Range read cancelled", true);
		}
		if (offset > reader.Size || size > reader.Size - offset) {
			throw VirtualMeshRangeError("Read exceeds file range");
		}
		auto bytes = reader.Read(offset, size, stop);
		if (bytes.size() != size) {
			throw VirtualMeshRangeError("Short range read");
		}
		return bytes;
	}
}

VirtualMeshRangeError::VirtualMeshRangeError(const char* message, bool cancelled)
	: std::runtime_error(message)
	, mCancelled(cancelled) { }

bool VirtualMeshRangeError::cancelled() const noexcept {
	return mCancelled;
}

VirtualMeshPageSource::VirtualMeshPageSource(VirtualMeshRangeReader reader, VMeshPagedMetadata metadata)
	: mReader(std::move(reader))
	, mMetadata(std::move(metadata)) { }
std::shared_ptr<const VirtualMeshPageSource> VirtualMeshPageSource::Open(VirtualMeshRangeReader reader, std::stop_token stop) {
	if (!reader.Read) {
		throw VMeshFormatError("Missing paged mesh backing");
	}
	const auto prefix = ReadExact(reader, 0, sizeof(VMeshPagedHeader), stop);
	const auto header = ReadVMeshPagedHeader(prefix, reader.Size);
	if (header.MetadataSize > MaxResidentMetadataBytes) {
		throw VMeshFormatError("Paged mesh metadata exceeds runtime allocation limit");
	}
	const auto bytes = ReadExact(reader, 0, static_cast<size_t>(header.MetadataSize), stop);
	auto metadata = ReadVMeshPagedMetadata(bytes, reader.Size);
	for (const auto& page : metadata.Pages) {
		if (page.Size > VMeshMaxPageBytes) {
			throw VMeshFormatError("Paged mesh page exceeds runtime allocation limit");
		}
	}
	return std::shared_ptr<const VirtualMeshPageSource>(new VirtualMeshPageSource(std::move(reader), std::move(metadata)));
}
const VMeshPagedMetadata& VirtualMeshPageSource::metadata() const {
	return mMetadata;
}
VMeshPageData VirtualMeshPageSource::readPage(uint32_t pageId, std::stop_token stop) const {
	if (pageId >= mMetadata.Pages.size()) {
		throw VMeshFormatError("Paged mesh page ID out of range");
	}
	const auto& page = mMetadata.Pages[pageId];
	const auto bytes = ReadExact(mReader, page.Offset, static_cast<size_t>(page.Size), stop);
	return ReadVMeshPage(mMetadata, pageId, bytes);
}
VMeshData VirtualMeshPageSource::readResident(std::stop_token stop) const {
	return MaterializeVMeshPages(mMetadata, [this, stop](uint32_t pageId) { return readPage(pageId, stop); });
}
}
