#include "virtualMeshLoader.h"

#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"

#include <iostream>
#include <stdexcept>

namespace IKIGAI::VG {
std::shared_ptr<VirtualMeshAsset> VirtualMeshLoader::CreateFromBytes(const std::string& path, const std::vector<uint8_t>& bytes) {
	try {
		const auto data = ReadVMesh(bytes);
		auto asset = std::make_shared<VirtualMeshAsset>(path, data, IsVirtualGeometryGpuPathSupported());
		std::cout << "VG_ASSET_LOADED path=" << path << " vertices=" << data.Vertices.size() << " indices=" << data.Indices.size()
				  << " clusters=" << data.Clusters.size() << " lods=" << data.Header.LodLevelCount << " slots=" << data.Header.MaterialSlotCount
				  << " leafTriangles=" << asset->leafTriangleCount() << " fallbackTriangles=" << asset->fallbackTriangleCount()
				  << " clusterBuffers=" << asset->hasClusterBuffers() << " gpuBytes=" << asset->gpuBytes() << std::endl;
		return asset;
	} catch (const std::exception& error) {
		throw std::runtime_error("Virtual mesh " + path + ": " + error.what());
	}
}

RESOURCES::ResourcePtr<VirtualMeshAsset> VirtualMeshLoader::createResource(const std::string& path) {
	auto file = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().getFile(path, RESOURCES::FileMode::READ);
	if (!file || !file->isValid()) {
		throw std::runtime_error("Virtual mesh " + path + ": file not found");
	}
	return CreateFromBytes(path, file->read());
}
}
