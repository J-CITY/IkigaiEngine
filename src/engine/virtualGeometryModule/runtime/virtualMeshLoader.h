#pragma once

#include "resourceModule/resourceManager.h"
#include "virtualMeshAsset.h"

namespace IKIGAI::VG {
// Resource manager for cooked virtual meshes (.vmesh). Reads the file through the virtual file system, validates
// it with ReadVMesh and creates the shared VirtualMeshAsset (cluster buffers only when the backend supports the
// GPU path). Failures throw with the file path and the failed check.
class VirtualMeshLoader : public RESOURCES::ResourceManager<VirtualMeshAsset> {
public:
	RESOURCES::ResourcePtr<VirtualMeshAsset> createResource(const std::string& path) override;
	// Parses and builds an asset from an in-memory image (tests and tools).
	static std::shared_ptr<VirtualMeshAsset> CreateFromBytes(const std::string& path, const std::vector<uint8_t>& bytes);

protected:
	bool reloadResource(std::weak_ptr<VirtualMeshAsset>, const std::string&) override {
		return false;
	}
};
}
