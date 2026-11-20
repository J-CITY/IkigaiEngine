#pragma once

#include "virtualGeometryModule/format/vmeshIO.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <vector>

namespace IKIGAI::VG {
class VirtualMeshRangeError final : public std::runtime_error {
public:
	explicit VirtualMeshRangeError(const char* message, bool cancelled = false);
	bool cancelled() const noexcept;

private:
	bool mCancelled = false;
};

struct VirtualMeshRangeReader {
	uint64_t Size = 0;
	// Return the requested byte count or throw VirtualMeshRangeError. Reads may run concurrently.
	// Retain the immutable backing for as long as this callback exists.
	std::function<std::vector<uint8_t>(uint64_t offset, std::size_t size, std::stop_token stop)> Read {};
};

// Immutable backing and validated resident metadata. Worker reads perform no GPU work.
class VirtualMeshPageSource {
public:
	static std::shared_ptr<const VirtualMeshPageSource> Open(VirtualMeshRangeReader reader, std::stop_token stop = {});
	const VMeshPagedMetadata& metadata() const;
	VMeshPageData readPage(uint32_t pageId, std::stop_token stop = {}) const;
	VMeshData readResident(std::stop_token stop = {}) const;

private:
	VirtualMeshPageSource(VirtualMeshRangeReader reader, VMeshPagedMetadata metadata);
	VirtualMeshRangeReader mReader = {};
	VMeshPagedMetadata mMetadata = {};
};
}
