#include "virtualMeshLoader.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include "virtualMeshPageSource.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace IKIGAI::VG {
namespace {
	std::shared_ptr<VirtualMeshAsset> CreateAsset(
		const std::string& path, const VMeshData& data, std::shared_ptr<const VirtualMeshPageSource> source = {}, bool visibilityBuffers = false) {
		auto asset = std::make_shared<VirtualMeshAsset>(path, data, IsVirtualGeometryGpuPathSupported(), std::move(source), visibilityBuffers);
		std::cout << "VG_ASSET_LOADED path=" << path << " vertices=" << data.Vertices.size() << " indices=" << data.Indices.size()
				  << " clusters=" << data.Clusters.size() << " lods=" << data.Header.LodLevelCount << " slots=" << data.Header.MaterialSlotCount
				  << " leafTriangles=" << asset->leafTriangleCount() << " fallbackTriangles=" << asset->fallbackTriangleCount()
				  << " clusterBuffers=" << asset->hasClusterBuffers() << " gpuBytes=" << asset->gpuBytes()
				  << " pagedSource=" << bool(asset->pageSource()) << std::endl;
		return asset;
	}
}
std::shared_ptr<VirtualMeshAsset> VirtualMeshLoader::CreateFromBytes(
	const std::string& path, const std::vector<uint8_t>& bytes, bool visibilityBuffers) {
	try {
		return CreateAsset(path, ReadVMesh(bytes), {}, visibilityBuffers);
	} catch (const std::exception& error) {
		throw std::runtime_error("Virtual mesh " + path + ": " + error.what());
	}
}
std::shared_ptr<VirtualMeshAsset> VirtualMeshLoader::loadStreamedResource(const std::string& path) {
	auto& files = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
	const auto normalized = NormalizePath(path);
	const auto key = std::to_string(files.generation()) + ":" + normalized;
	std::erase_if(mStreamedAssets, [](const auto& entry) { return entry.second.expired(); });
	if (auto cached = mStreamedAssets[key].lock()) {
		return cached;
	}
	try {
		auto range = files.openRange(normalized);
		const auto prefix = range->read(0, sizeof(uint32_t) * 2);
		uint32_t version = 0;
		std::memcpy(&version, prefix.data() + sizeof(uint32_t), sizeof(version));
		if (version == VMeshVersion) {
			std::cout << "VG_STREAM_RESIDENT_COMPAT path=" << path << " reason=v1" << std::endl;
			return loadResource(normalized, RESOURCES::ELoadingType::FILE);
		}
		auto source = VirtualMeshPageSource::Open(std::move(range));
		auto asset = std::make_shared<VirtualMeshAsset>(normalized, std::move(source));
		mStreamedAssets[key] = asset;
		return asset;
	} catch (const RESOURCES::RangeFileError& error) {
		if (error.code() == RESOURCES::RangeFileErrorCode::UnsupportedBacking) {
			std::cout << "VG_STREAM_RESIDENT_COMPAT path=" << path << " reason=" << error.what() << std::endl;
			return loadResource(normalized, RESOURCES::ELoadingType::FILE);
		}
		throw std::runtime_error("Streamed virtual mesh " + path + ": " + error.what());
	} catch (const std::exception& error) {
		throw std::runtime_error("Streamed virtual mesh " + path + ": " + error.what());
	}
}

std::shared_ptr<VirtualMeshAsset> VirtualMeshLoader::loadVisibilityResource(const std::string& path) {
	auto& files = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
	const auto normalized = NormalizePath(path);
	const auto key = std::to_string(files.generation()) + ":" + normalized;
	std::erase_if(mVisibilityAssets, [](const auto& entry) { return entry.second.expired(); });
	if (auto cached = mVisibilityAssets[key].lock()) {
		return cached;
	}
	auto asset = createResidentResource(normalized, true);
	mVisibilityAssets[key] = asset;
	return asset;
}

RESOURCES::ResourcePtr<VirtualMeshAsset> VirtualMeshLoader::createResource(const std::string& path) {
	return createResidentResource(path, false);
}

std::shared_ptr<VirtualMeshAsset> VirtualMeshLoader::createResidentResource(const std::string& path, bool visibilityBuffers) {
	auto& files = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
	std::shared_ptr<const RESOURCES::RangeFile> range;
	try {
		range = files.openRange(path);
	} catch (const RESOURCES::RangeFileError& error) {
		if (error.code() != RESOURCES::RangeFileErrorCode::UnsupportedBacking) {
			throw std::runtime_error("Virtual mesh " + path + ": " + error.what());
		}
		// Compatibility path for compressed archives/platform mounts. No streaming source is exposed.
		std::cout << "VG_RESIDENT_IO_FALLBACK path=" << path << " reason=" << error.what() << std::endl;
	}
	if (range) {
		try {
			const auto prefix = range->read(0, sizeof(uint32_t) * 2u);
			uint32_t version = 0;
			std::memcpy(&version, prefix.data() + sizeof(uint32_t), sizeof(version));
			if (version == VMeshPagedVersion) {
				auto source = VirtualMeshPageSource::Open(range);
				const auto data = source->readResident();
				return CreateAsset(path, data, std::move(source), visibilityBuffers);
			}
		} catch (const std::exception& error) {
			throw std::runtime_error("Virtual mesh " + path + ": " + error.what());
		}
	}
	auto file = files.getFile(path, RESOURCES::FileMode::READ);
	if (!file || !file->isValid()) {
		throw std::runtime_error("Virtual mesh " + path + ": file not found");
	}
	return CreateFromBytes(path, file->read(), visibilityBuffers);
}
}
