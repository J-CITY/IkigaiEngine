#include "virtualMeshPageSource.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include <utility>
namespace IKIGAI::VG {
namespace {
	constexpr uint64_t MaxResidentMetadataBytes = 256ull * 1024ull * 1024ull;
}
VirtualMeshPageSource::VirtualMeshPageSource(std::shared_ptr<const RESOURCES::RangeFile> file, VMeshPagedMetadata metadata)
	: mFile(std::move(file))
	, mMetadata(std::move(metadata)) { }
std::shared_ptr<const VirtualMeshPageSource> VirtualMeshPageSource::Open(std::shared_ptr<const RESOURCES::RangeFile> file, std::stop_token stop) {
	if (!file) {
		throw VMeshFormatError("Missing paged mesh backing");
	}
	const auto prefix = file->read(0, sizeof(VMeshPagedHeader), stop);
	const auto header = ReadVMeshPagedHeader(prefix, file->size());
	if (header.MetadataSize > MaxResidentMetadataBytes) {
		throw VMeshFormatError("Paged mesh metadata exceeds runtime allocation limit");
	}
	const auto bytes = file->read(0, static_cast<size_t>(header.MetadataSize), stop);
	auto metadata = ReadVMeshPagedMetadata(bytes, file->size());
	for (const auto& page : metadata.Pages) {
		if (page.Size > VMeshMaxPageBytes) {
			throw VMeshFormatError("Paged mesh page exceeds runtime allocation limit");
		}
	}
	return std::shared_ptr<const VirtualMeshPageSource>(new VirtualMeshPageSource(std::move(file), std::move(metadata)));
}
const VMeshPagedMetadata& VirtualMeshPageSource::metadata() const {
	return mMetadata;
}
VMeshPageData VirtualMeshPageSource::readPage(uint32_t pageId, std::stop_token stop) const {
	if (pageId >= mMetadata.Pages.size()) {
		throw VMeshFormatError("Paged mesh page ID out of range");
	}
	const auto& page = mMetadata.Pages[pageId];
	const auto bytes = mFile->read(page.Offset, static_cast<size_t>(page.Size), stop);
	return ReadVMeshPage(mMetadata, pageId, bytes);
}
VMeshData VirtualMeshPageSource::readResident(std::stop_token stop) const {
	return MaterializeVMeshPages(mMetadata, [this, stop](uint32_t pageId) { return readPage(pageId, stop); });
}
}
