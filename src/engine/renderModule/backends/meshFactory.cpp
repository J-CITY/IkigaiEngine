#include "meshFactory.h"
#include "interface/driverInterface.h"
#include "interface/universalMesh.h"
#include "utilsModule/memory/resourceAllocator.h"
#include <stdexcept>

#ifdef OPENGL_BACKEND
#include "coreModule/graphicsWrapper.hpp"
#include "gl/indexBufferGl.h"
#include "gl/meshGl.h"
#include "gl/modelGl.h"

#endif
#ifdef VULKAN_BACKEND
#include "vk/indexBufferVk.h"
#include "vk/meshVk.h"
#include "vk/modelVk.h"
#include "vk/vertexBufferVk.h"

#endif
#ifdef DX12_BACKEND
#include "dx12/indexBufferDx12.h"
#include "dx12/meshDx12.h"
#include "dx12/modelDx12.h"
#include "dx12/vertexBufferDx12.h"

#endif
#ifdef METAL_BACKEND
namespace IKIGAI::RENDER {
std::shared_ptr<ModelInterface> CreateEmptyModelMetal(const std::string& path);
std::shared_ptr<MeshInterface> CreateMeshMetal(
	const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, size_t offset, unsigned materialIndex);
std::shared_ptr<MeshInterface> CreateMeshMetal(const MeshData& data, unsigned materialIndex);
}
#endif

namespace IKIGAI::RENDER {
namespace {
	class CustomMesh final : public UniversalMesh {
#ifdef OPENGL_BACKEND
		std::unique_ptr<VertexArray> mVao;
#endif
	public:
		CustomMesh(const MeshData& data, unsigned materialIndex)
			: UniversalMesh(data, materialIndex) {
			std::vector<uint32_t> indices(data.indices.begin(), data.indices.end());
			for (const auto& stream : data.streams) {
				const auto stride = mLayout.stream(stream.binding).stride;
				std::shared_ptr<VertexBufferInterface> buffer;
				switch (DriverInterface::settings.backend) {
#ifdef OPENGL_BACKEND
				case RenderSettings::Backend::OPENGL:
					buffer = std::make_shared<VertexBufferGl>(stream.bytes.data(), stream.count, stride, VertexBufferGl::UsageType::DYNAMIC_DRAW);
					break;
#endif
#ifdef VULKAN_BACKEND
				case RenderSettings::Backend::VULKAN:
					buffer = std::make_shared<VertexBufferVk>(const_cast<std::byte*>(stream.bytes.data()), stream.count, stride);
					break;
#endif
#ifdef DX12_BACKEND
				case RenderSettings::Backend::DIRECTX12:
					buffer = std::make_shared<VertexBufferDx12>(const_cast<std::byte*>(stream.bytes.data()), stream.count, stride);
					break;
#endif
				default:
					throw std::runtime_error("Custom vertex mesh backend is unavailable");
				}
				mStreams.emplace_back(stream.binding, std::move(buffer));
			}
			switch (DriverInterface::settings.backend) {
#ifdef OPENGL_BACKEND
			case RenderSettings::Backend::OPENGL: {
				mVao = std::make_unique<VertexArray>();
				mVao->bind();
				if (!indices.empty())
					mIndexBuffer = std::make_shared<IndexBufferGl>(indices);
				for (const auto& a : mLayout.attributes) {
					const auto& s = mLayout.stream(a.binding);
					const auto info = GetVertexFormatInfo(a.format);
					for (const auto& [binding, buffer] : mStreams)
						if (binding == a.binding) {
							buffer->bind();
							const GLenum type = info.normalized				? (a.format == VertexFormat::UNorm8x4 ? GL_UNSIGNED_BYTE : GL_BYTE)
								: info.shaderType == VertexScalarType::Int	? GL_INT
								: info.shaderType == VertexScalarType::UInt ? GL_UNSIGNED_INT
																			: GL_FLOAT;
#if defined(USING_GLES) && defined(IKIGAI_GLES_VERSION) && IKIGAI_GLES_VERSION < 300
							if (info.shaderType != VertexScalarType::Float || s.rate == VertexInputRate::PerInstance)
								throw std::runtime_error("Custom integer/instance attributes require GLES3 or desktop GL");
#else
							if (!info.normalized && info.shaderType != VertexScalarType::Float)
								glVertexAttribIPointer(
									a.location, info.components, type, s.stride, reinterpret_cast<const void*>(uintptr_t(a.offset)));
							else
#endif
							glVertexAttribPointer(a.location, info.components, type, info.normalized ? GL_TRUE : GL_FALSE, s.stride,
								reinterpret_cast<const void*>(uintptr_t(a.offset)));
							glEnableVertexAttribArray(a.location);
#if !defined(USING_GLES) || !defined(IKIGAI_GLES_VERSION) || IKIGAI_GLES_VERSION >= 300
							glVertexAttribDivisor(a.location, s.rate == VertexInputRate::PerInstance ? s.stepRate : 0);
#endif
						}
				}
				if (mIndexBuffer)
					mIndexBuffer->bind();
				mVao->unbind();
				break;
			}
#endif
#ifdef VULKAN_BACKEND
			case RenderSettings::Backend::VULKAN:
				if (!indices.empty())
					mIndexBuffer = std::make_shared<IndexBufferVk>(indices);
				break;
#endif
#ifdef DX12_BACKEND
			case RenderSettings::Backend::DIRECTX12:
				if (!indices.empty())
					mIndexBuffer = std::make_shared<IndexBufferDx12>(indices);
				break;
#endif
			default:
				break;
			}
		}
		void bind() const override {
			UniversalMesh::bind();
#ifdef OPENGL_BACKEND
			if (mVao)
				mVao->bind();
#endif
		}
	};
}
std::shared_ptr<MeshInterface> CreateMesh(const MeshData& data, unsigned materialIndex) {
#ifdef METAL_BACKEND
	if (DriverInterface::settings.backend == RenderSettings::Backend::METAL)
		return CreateMeshMetal(data, materialIndex);
#endif
	return IKIGAI::MEMORY::MakeShared<CustomMesh, IKIGAI::MEMORY::Category::Mesh>(data, materialIndex);
}

std::shared_ptr<VertexBufferInterface> CreateGpuDrivenVertexBuffer(
	[[maybe_unused]] const void* data, [[maybe_unused]] size_t count, [[maybe_unused]] size_t stride) {
	switch (DriverInterface::settings.backend) {
#ifdef VULKAN_BACKEND
	case RenderSettings::Backend::VULKAN:
		return std::make_shared<VertexBufferVk>(const_cast<void*>(data), count, stride);
#endif
#ifdef DX12_BACKEND
	case RenderSettings::Backend::DIRECTX12:
		return std::make_shared<VertexBufferDx12>(const_cast<void*>(data), count, stride);
#endif
	default:
		throw std::runtime_error("Virtual geometry vertex buffers require Vulkan or DirectX 12");
	}
}

std::shared_ptr<IndexBufferInterface> CreateGpuDrivenIndexBuffer([[maybe_unused]] const uint32_t* data, [[maybe_unused]] size_t count) {
	switch (DriverInterface::settings.backend) {
#ifdef VULKAN_BACKEND
	case RenderSettings::Backend::VULKAN:
		return std::make_shared<IndexBufferVk>(const_cast<uint32_t*>(data), count, sizeof(uint32_t));
#endif
#ifdef DX12_BACKEND
	case RenderSettings::Backend::DIRECTX12:
		return std::make_shared<IndexBufferDx12>(const_cast<uint32_t*>(data), count, sizeof(uint32_t));
#endif
	default:
		throw std::runtime_error("Virtual geometry index buffers require Vulkan or DirectX 12");
	}
}

std::vector<StreamingMeshBufferSlot> CreateStreamingMeshBufferSlots(
	size_t slotCount, size_t vertexCapacity, size_t vertexStride, size_t indexCapacity) {
	std::vector<uint32_t> initialIndices;
	switch (DriverInterface::settings.backend) {
#ifdef VULKAN_BACKEND
	case RenderSettings::Backend::VULKAN:
		break;
#endif
#ifdef DX12_BACKEND
	case RenderSettings::Backend::DIRECTX12:
		// Keep the initial DX12 upload, sharing one zero-filled staging vector across all slots.
		initialIndices.resize(indexCapacity);
		break;
#endif
	default:
		throw std::runtime_error("VG GPU page pool requires Vulkan or DirectX 12");
	}

	const auto* indices = initialIndices.empty() ? nullptr : initialIndices.data();
	std::vector<StreamingMeshBufferSlot> slots;
	slots.reserve(slotCount);
	for (size_t slotIndex = 0; slotIndex < slotCount; ++slotIndex) {
		slots.push_back({ CreateGpuDrivenVertexBuffer(nullptr, vertexCapacity, vertexStride), CreateGpuDrivenIndexBuffer(indices, indexCapacity) });
	}
	return slots;
}

std::shared_ptr<ModelInterface> CreateEmptyModel(const std::string& path) {
	switch (DriverInterface::settings.backend) {
#ifdef OPENGL_BACKEND
	case RenderSettings::Backend::OPENGL:
		return IKIGAI::MEMORY::MakeShared<ModelGl, IKIGAI::MEMORY::Category::Model>(path);
#endif
#ifdef VULKAN_BACKEND
	case RenderSettings::Backend::VULKAN:
		return IKIGAI::MEMORY::MakeShared<ModelVk, IKIGAI::MEMORY::Category::Model>(path);
#endif
#ifdef DX12_BACKEND
	case RenderSettings::Backend::DIRECTX12:
		return IKIGAI::MEMORY::MakeShared<ModelDx12, IKIGAI::MEMORY::Category::Model>(path);
#endif
#ifdef METAL_BACKEND
	case RenderSettings::Backend::METAL:
		return CreateEmptyModelMetal(path);
#endif
	default:
		return nullptr;
	}
}

std::shared_ptr<MeshInterface> CreateMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, unsigned materialIndex) {
	return CreateMesh(vertices, indices, static_cast<size_t>(0), materialIndex);
}

std::shared_ptr<MeshInterface> CreateMesh(
	const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, size_t offset, unsigned materialIndex) {
	switch (DriverInterface::settings.backend) {
#ifdef OPENGL_BACKEND
	case RenderSettings::Backend::OPENGL:
		if (offset != 0) {
			return IKIGAI::MEMORY::MakeShared<MeshGl, IKIGAI::MEMORY::Category::Mesh>(vertices, indices, offset, materialIndex);
		}
		return IKIGAI::MEMORY::MakeShared<MeshGl, IKIGAI::MEMORY::Category::Mesh>(vertices, indices, materialIndex);
#endif
#ifdef VULKAN_BACKEND
	case RenderSettings::Backend::VULKAN:
		if (offset != 0) {
			return IKIGAI::MEMORY::MakeShared<MeshVk, IKIGAI::MEMORY::Category::Mesh>(vertices, indices, offset, materialIndex);
		}
		return IKIGAI::MEMORY::MakeShared<MeshVk, IKIGAI::MEMORY::Category::Mesh>(vertices, indices, materialIndex);
#endif
#ifdef DX12_BACKEND
	case RenderSettings::Backend::DIRECTX12:
		return IKIGAI::MEMORY::MakeShared<MeshDx12, IKIGAI::MEMORY::Category::Mesh>(vertices, indices, materialIndex);
#endif
#ifdef METAL_BACKEND
	case RenderSettings::Backend::METAL:
		return CreateMeshMetal(vertices, indices, offset, materialIndex);
#endif
	default:
		return nullptr;
	}
}
}
