#include "virtualMeshAsset.h"

#include "renderModule/backends/interface/driverInterface.h"
#include "renderModule/backends/interface/indexBufferInterface.h"
#include "renderModule/backends/interface/meshInterface.h"
#include "renderModule/backends/interface/modelInterface.h"
#include "renderModule/backends/interface/storageBufferInterface.h"
#include "renderModule/backends/interface/vertexBufferInterface.h"
#include "renderModule/backends/meshFactory.h"
#include "virtualGeometryModule/format/vmeshEncoding.h"

#ifdef VULKAN_BACKEND
#include "renderModule/backends/vk/indexBufferVk.h"
#include "renderModule/backends/vk/vertexBufferVk.h"
#endif
#ifdef DX12_BACKEND
#include "renderModule/backends/dx12/indexBufferDx12.h"
#include "renderModule/backends/dx12/vertexBufferDx12.h"
#endif

#include <stdexcept>
#include <unordered_map>

namespace IKIGAI::VG {
namespace {
	struct TriangleRange {
		uint32_t MaterialSlot = 0;
		uint32_t IndexOffset = 0;
		uint32_t IndexCount = 0;
	};

	MATH::Vector3f ToVector(const VMeshFloat3& value) {
		return { value.X, value.Y, value.Z };
	}

	Vertex DecodeVertex(const VMeshVertex& source) {
		const auto normal = ToVector(DecodeNormal(source.Normal));
		const auto tangent = DecodeTangent(source.Tangent);
		const auto tangentVector = ToVector(tangent.Tangent);
		const auto bitangent = MATH::Vector3f::Cross(normal, tangentVector) * tangent.BitangentSign;
		return Vertex(
			{ source.Position[0], source.Position[1], source.Position[2] }, { source.Uv[0], source.Uv[1] }, normal, tangentVector, bitangent);
	}

	std::shared_ptr<RENDER::VertexBufferInterface> CreateVertexBuffer(const void* data, size_t count, size_t stride) {
		switch (RENDER::DriverInterface::settings.backend) {
#ifdef VULKAN_BACKEND
		case RENDER::RenderSettings::Backend::VULKAN:
			return std::make_shared<RENDER::VertexBufferVk>(const_cast<void*>(data), count, stride);
#endif
#ifdef DX12_BACKEND
		case RENDER::RenderSettings::Backend::DIRECTX12:
			return std::make_shared<RENDER::VertexBufferDx12>(const_cast<void*>(data), count, stride);
#endif
		default:
			throw std::runtime_error("Virtual geometry vertex buffers require Vulkan or DirectX 12");
		}
	}

	std::shared_ptr<RENDER::IndexBufferInterface> CreateIndexBuffer(const std::vector<uint32_t>& indices) {
		switch (RENDER::DriverInterface::settings.backend) {
#ifdef VULKAN_BACKEND
		case RENDER::RenderSettings::Backend::VULKAN:
			return std::make_shared<RENDER::IndexBufferVk>(indices);
#endif
#ifdef DX12_BACKEND
		case RENDER::RenderSettings::Backend::DIRECTX12:
			return std::make_shared<RENDER::IndexBufferDx12>(indices);
#endif
		default:
			throw std::runtime_error("Virtual geometry index buffers require Vulkan or DirectX 12");
		}
	}
}

std::shared_ptr<RENDER::VertexBufferInterface> CreateVirtualGeometryVertexBuffer(const void* data, size_t count, size_t stride) {
	return CreateVertexBuffer(data, count, stride);
}

bool IsVirtualGeometryGpuPathSupported() {
	auto* driver = RENDER::DriverInterface::Get();
	if (!driver) {
		return false;
	}
	const auto backend = RENDER::DriverInterface::settings.backend;
	if (backend != RENDER::RenderSettings::Backend::VULKAN && backend != RENDER::RenderSettings::Backend::DIRECTX12) {
		return false;
	}
	const auto features = driver->gpuFeatures();
	return features.DrawIndirect && features.BufferUsageFlags && features.MaxDrawIndirectCount > 0;
}

VirtualMeshAsset::VirtualMeshAsset(std::string path, const VMeshData& data, bool createClusterBuffersFlag)
	: mPath(std::move(path))
	, mHeader(data.Header)
	, mClusters(data.Clusters) {
	for (const auto& cluster : mClusters) {
		if (cluster.LodLevel == 0) {
			mLeafTriangleCount += cluster.IndexCount / 3;
		}
	}
	createFallbackModel(data);
	if (createClusterBuffersFlag) {
		createClusterBuffers(data);
	}
}

VirtualMeshAsset::~VirtualMeshAsset() = default;

const std::string& VirtualMeshAsset::path() const {
	return mPath;
}

const VMeshHeader& VirtualMeshAsset::header() const {
	return mHeader;
}

const std::vector<VMeshCluster>& VirtualMeshAsset::clusters() const {
	return mClusters;
}

uint32_t VirtualMeshAsset::materialSlotCount() const {
	return mHeader.MaterialSlotCount;
}

uint32_t VirtualMeshAsset::leafTriangleCount() const {
	return mLeafTriangleCount;
}

uint32_t VirtualMeshAsset::fallbackTriangleCount() const {
	return mFallbackTriangleCount;
}

bool VirtualMeshAsset::hasClusterBuffers() const {
	return mGpu.Vertices && mGpu.Indices;
}

const VirtualMeshGpuBuffers& VirtualMeshAsset::gpuBuffers() const {
	return mGpu;
}

const std::shared_ptr<RENDER::ModelInterface>& VirtualMeshAsset::fallbackModel() const {
	return mFallbackModel;
}

size_t VirtualMeshAsset::gpuBytes() const {
	return mGpuBytes;
}

void VirtualMeshAsset::createFallbackModel(const VMeshData& data) {
	// Fallback ranges are optional in the format; the coarsest DAG level (root clusters) is an equivalent
	// crack-free representation when the cooker did not emit them.
	std::vector<TriangleRange> ranges;
	for (const auto& range : data.FallbackRanges) {
		ranges.push_back({ range.MaterialSlot, range.IndexOffset, range.IndexCount });
	}
	if (ranges.empty()) {
		for (const auto& cluster : data.Clusters) {
			if (cluster.Flags & VMESH_CLUSTER_ROOT) {
				ranges.push_back({ cluster.MaterialSlot, cluster.IndexOffset, cluster.IndexCount });
			}
		}
	}
	std::vector<std::shared_ptr<RENDER::MeshInterface>> meshes;
	for (uint32_t slot = 0; slot < data.Header.MaterialSlotCount; ++slot) {
		std::vector<Vertex> vertices;
		std::vector<unsigned> indices;
		std::unordered_map<uint32_t, unsigned> remap;
		for (const auto& range : ranges) {
			if (range.MaterialSlot != slot) {
				continue;
			}
			if (uint64_t(range.IndexOffset) + range.IndexCount > data.Indices.size() || range.IndexCount % 3 != 0) {
				throw std::runtime_error("Virtual mesh fallback range exceeds the index section: " + mPath);
			}
			for (uint32_t i = 0; i < range.IndexCount; ++i) {
				const uint32_t source = data.Indices[range.IndexOffset + i];
				if (source >= data.Vertices.size()) {
					throw std::runtime_error("Virtual mesh fallback index exceeds the vertex section: " + mPath);
				}
				const auto [found, inserted] = remap.try_emplace(source, static_cast<unsigned>(vertices.size()));
				if (inserted) {
					vertices.push_back(DecodeVertex(data.Vertices[source]));
				}
				indices.push_back(found->second);
			}
		}
		if (indices.empty()) {
			continue;
		}
		mFallbackTriangleCount += static_cast<uint32_t>(indices.size() / 3);
		if (auto mesh = RENDER::CreateMesh(vertices, indices, slot)) {
			meshes.push_back(std::move(mesh));
		}
	}
	mFallbackModel = RENDER::CreateEmptyModel(mPath);
	if (!mFallbackModel) {
		return;
	}
	mFallbackModel->setMeshes(meshes);
	std::vector<std::string> names;
	for (uint32_t slot = 0; slot < data.Header.MaterialSlotCount; ++slot) {
		names.push_back("slot" + std::to_string(slot));
	}
	mFallbackModel->setMaterialNames(names);
	if (!meshes.empty()) {
		mFallbackModel->computeBoundingSphere();
	}
}

void VirtualMeshAsset::createClusterBuffers(const VMeshData& data) {
	auto* driver = RENDER::DriverInterface::Get();
	if (!driver || data.Vertices.empty() || data.Indices.empty()) {
		return;
	}
	mGpu.Vertices = CreateVertexBuffer(data.Vertices.data(), data.Vertices.size(), sizeof(VMeshVertex));
	mGpu.Indices = CreateIndexBuffer(data.Indices);
	mGpu.Clusters = driver->createStorageBuffer(data.Clusters.data(), data.Clusters.size(), sizeof(VMeshCluster), RENDER::BufferUsage::STORAGE);
	mGpuBytes = data.Vertices.size() * sizeof(VMeshVertex) + data.Indices.size() * sizeof(uint32_t) + data.Clusters.size() * sizeof(VMeshCluster);
}
}
