#include "meshDx12.h"
#ifdef DX12_BACKEND

#include "driverDx12.h"
#include "indexBufferDx12.h"
#include "renderModule/render.h"
#include "vertexBufferDx12.h"
#include <cstddef>
#include <limits>
#include <numeric>

namespace IKIGAI::RENDER {
namespace {
	class PooledMeshDx12 final : public MeshInterface {
	public:
		PooledMeshDx12(size_t vertices, size_t indices) {
			mVertexCount = vertices;
			mIndicesCount = indices;
			mLayout.streams = { { 0, sizeof(Vertex), VertexInputRate::PerVertex, 1 }, { 1, sizeof(uint32_t), VertexInputRate::PerInstance, 1 } };
			mLayout.attributes
				= { { 0, 0, VertexFormat::Float3, offsetof(Vertex, position) }, { 1, 0, VertexFormat::Float2, offsetof(Vertex, texCoord) },
					  { 2, 0, VertexFormat::Float3, offsetof(Vertex, normal) }, { 3, 0, VertexFormat::Float3, offsetof(Vertex, tangent) },
					  { 4, 0, VertexFormat::Float3, offsetof(Vertex, bitangent) }, { 5, 0, VertexFormat::Int4, offsetof(Vertex, m_BoneIDs) },
					  { 6, 0, VertexFormat::Float4, offsetof(Vertex, m_Weights) }, { 7, 1, VertexFormat::UInt, 0 } };
			mLayout.validate();
		}
		void initialize(
			std::shared_ptr<VertexBufferDx12> vertices, std::shared_ptr<VertexBufferDx12> instances, std::shared_ptr<IndexBufferDx12> indices) {
			mStreams = { { 0, std::move(vertices) }, { 1, std::move(instances) } };
			mIndices = std::move(indices);
		}
		void bind() const override {
			auto* driver = DriverInterface::Get();
			for (const auto& [binding, stream] : mStreams) {
				static_cast<VertexBufferDx12&>(*stream).transition(
					d3dUtil::GetDriver()->getCommandList().Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			}
			mIndices->transition(d3dUtil::GetDriver()->getCommandList().Get(), D3D12_RESOURCE_STATE_INDEX_BUFFER);
			driver->setVertexStreams(mLayout, mStreams);
			driver->setIndexBuffer(mIndices);
		}
		void unbind() const override { }
		const VertexLayout* vertexLayout() const override {
			return &mLayout;
		}

	private:
		VertexLayout mLayout;
		std::vector<std::pair<uint32_t, std::shared_ptr<VertexBufferInterface>>> mStreams;
		std::shared_ptr<IndexBufferDx12> mIndices;
	};
}

bool MeshDx12::SupportsGeometryPool(const MeshInterface& mesh) {
	const auto* source = dynamic_cast<const MeshDx12*>(&mesh);
	if (!source || !source->mVertexBuffer || !source->mIndexBuffer || mesh.vertexLayout() || mesh.getOffset() != 0) {
		return false;
	}
	const auto vertices = std::dynamic_pointer_cast<VertexBufferDx12>(source->mVertexBuffer);
	const auto indices = std::dynamic_pointer_cast<IndexBufferDx12>(source->mIndexBuffer);
	return vertices && indices && vertices->getStride() == sizeof(Vertex) && indices->getStride() == sizeof(uint32_t) && mesh.getVertexCount() != 0
		&& mesh.getIndexCount() != 0 && mesh.getVertexCount() <= static_cast<size_t>(std::numeric_limits<int32_t>::max())
		&& mesh.getIndexCount() <= std::numeric_limits<uint32_t>::max() && vertices->getSize() == mesh.getVertexCount()
		&& indices->getSize() == mesh.getIndexCount();
}

MeshDx12::GeometryPool MeshDx12::CreateGeometryPool(Renderer& renderer, std::span<const std::shared_ptr<MeshInterface>> meshes) {
	GeometryPool result;
	if (meshes.empty()) {
		return result;
	}
	struct Source {
		std::shared_ptr<VertexBufferDx12> vertices;
		std::shared_ptr<IndexBufferDx12> indices;
	};
	std::vector<Source> sources;
	size_t totalVertices = 0;
	size_t totalIndices = 0;
	for (const auto& mesh : meshes) {
		if (!mesh || !SupportsGeometryPool(*mesh)) {
			return {};
		}
		const auto* source = static_cast<const MeshDx12*>(mesh.get());
		const size_t vertices = mesh->getVertexCount();
		const size_t indices = mesh->getIndexCount();
		if (vertices > std::numeric_limits<int32_t>::max() - totalVertices || indices > std::numeric_limits<uint32_t>::max() - totalIndices
			|| totalVertices + vertices > std::numeric_limits<uint32_t>::max() / sizeof(Vertex)
			|| totalIndices + indices > std::numeric_limits<uint32_t>::max() / sizeof(uint32_t)) {
			return {};
		}
		result.Ranges.push_back({ static_cast<uint32_t>(indices), static_cast<uint32_t>(totalIndices), static_cast<int32_t>(totalVertices) });
		sources.push_back(
			{ std::static_pointer_cast<VertexBufferDx12>(source->mVertexBuffer), std::static_pointer_cast<IndexBufferDx12>(source->mIndexBuffer) });
		totalVertices += vertices;
		totalIndices += indices;
	}
	auto pool = std::make_shared<PooledMeshDx12>(totalVertices, totalIndices);
	result.Mesh = pool;
	renderer.execute([pool, sources = std::move(sources), totalVertices, totalIndices] {
		auto vertices = std::make_shared<VertexBufferDx12>(nullptr, totalVertices, sizeof(Vertex));
		auto indices = std::make_shared<IndexBufferDx12>(nullptr, totalIndices, sizeof(uint32_t));
		std::vector<uint32_t> instanceIds(65536);
		std::iota(instanceIds.begin(), instanceIds.end(), 0u);
		auto instances = std::make_shared<VertexBufferDx12>(instanceIds);
		auto* cmd = d3dUtil::GetDriver()->getCommandList().Get();
		vertices->transition(cmd, D3D12_RESOURCE_STATE_COPY_DEST);
		indices->transition(cmd, D3D12_RESOURCE_STATE_COPY_DEST);
		uint64_t vertexOffset = 0;
		uint64_t indexOffset = 0;
		for (const auto& source : sources) {
			source.vertices->transition(cmd, D3D12_RESOURCE_STATE_COPY_SOURCE);
			source.indices->transition(cmd, D3D12_RESOURCE_STATE_COPY_SOURCE);
			cmd->CopyBufferRegion(vertices->getBuffer().Get(), vertexOffset, source.vertices->getBuffer().Get(), 0, source.vertices->getSizeByte());
			cmd->CopyBufferRegion(indices->getBuffer().Get(), indexOffset, source.indices->getBuffer().Get(), 0, source.indices->getSizeByte());
			source.vertices->transition(cmd, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			source.indices->transition(cmd, D3D12_RESOURCE_STATE_INDEX_BUFFER);
			vertexOffset += source.vertices->getSizeByte();
			indexOffset += source.indices->getSizeByte();
		}
		vertices->transition(cmd, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		indices->transition(cmd, D3D12_RESOURCE_STATE_INDEX_BUFFER);
		pool->initialize(std::move(vertices), std::move(instances), std::move(indices));
	});
	return result;
}
}

IKIGAI::RENDER::MeshDx12::MeshDx12(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, unsigned materialIndex) {
	if (d3dUtil::GetDriver()->rayTracingCapabilities().AccelerationStructure) {
		mRayVertices = vertices;
		mRayIndices = indices;
	}
	mVertexBuffer = std::make_shared<VertexBufferDx12>(vertices);
	mIndexBuffer = std::make_shared<IndexBufferDx12>(indices);
	mIndicesCount = mIndexBuffer->getSize();
	mVertexCount = mVertexBuffer->getSize();
	mMaterialIndex = materialIndex;
	computeBoundingSphere(vertices);
}

IKIGAI::RENDER::MeshDx12::MeshDx12(const std::vector<Vertex>& vertices, unsigned materialIndex) {
	if (d3dUtil::GetDriver()->rayTracingCapabilities().AccelerationStructure) {
		mRayVertices = vertices;
	}
	mVertexBuffer = std::make_shared<VertexBufferDx12>(vertices);
	mVertexCount = mVertexBuffer->getSize();
	mMaterialIndex = materialIndex;
	computeBoundingSphere(vertices);
}

void IKIGAI::RENDER::MeshDx12::bind() const {
	mVertexBuffer->bind();
	if (mIndexBuffer) {
		mIndexBuffer->bind();
	}
}

void IKIGAI::RENDER::MeshDx12::unbind() const {
	mVertexBuffer->unbind();
	if (mIndexBuffer) {
		mIndexBuffer->unbind();
	}
}
#endif
