#pragma once

#include "virtualGeometryModule/format/vmeshIO.h"
#include <memory>
#include <stop_token>

namespace IKIGAI::RESOURCES {
class RangeFile;
}

namespace IKIGAI::VG {
// Immutable backing and validated resident metadata. Worker reads perform no GPU work.
class VirtualMeshPageSource {
public:
	static std::shared_ptr<const VirtualMeshPageSource> Open(std::shared_ptr<const RESOURCES::RangeFile> file, std::stop_token stop = {});
	const VMeshPagedMetadata& metadata() const;
	VMeshPageData readPage(uint32_t pageId, std::stop_token stop = {}) const;
	VMeshData readResident(std::stop_token stop = {}) const;

private:
	VirtualMeshPageSource(std::shared_ptr<const RESOURCES::RangeFile> file, VMeshPagedMetadata metadata);
	std::shared_ptr<const RESOURCES::RangeFile> mFile = {};
	VMeshPagedMetadata mMetadata = {};
};
}
