#pragma once

#include "resourceModule/resourceManager.h"
#include "virtualMeshAsset.h"

namespace IKIGAI::VG {
// Loads v1 and paged v2 meshes as fully resident assets. Supported v2 backing retains
// an immutable range source for later streaming; other mounts use an explicit resident-only path.
// Failures include the file path and the validation error.
class VirtualMeshLoader : public RESOURCES::ResourceManager<VirtualMeshAsset> {
public:
	RESOURCES::ResourcePtr<VirtualMeshAsset> createResource(const std::string& path) override;
	// Parses and builds an asset from an in-memory image (tests and tools).
	std::shared_ptr<VirtualMeshAsset> loadStreamedResource(const std::string& path);
	std::shared_ptr<VirtualMeshAsset> loadVisibilityResource(const std::string& path);
	static std::shared_ptr<VirtualMeshAsset> CreateFromBytes(
		const std::string& path, const std::vector<uint8_t>& bytes, bool visibilityBuffers = false);

private:
	std::shared_ptr<VirtualMeshAsset> createResidentResource(const std::string& path, bool visibilityBuffers);
	std::unordered_map<std::string, std::weak_ptr<VirtualMeshAsset>> mStreamedAssets {};
	std::unordered_map<std::string, std::weak_ptr<VirtualMeshAsset>> mVisibilityAssets {};

protected:
	bool reloadResource(std::weak_ptr<VirtualMeshAsset>, const std::string&) override {
		return false;
	}
};
}
