#pragma once

#include <bitset>

#include "../interface/shaderInterface.h"
#include <memory>

#include "resourceStruct.h"
#include "storageBufferInterface.h"
#include "textureInterface.h"
#include "meshInterface.h"
#include "modelInterface.h"
#include "materialInterface.h"
#include "frameBufferInterface.h"
#include <utilsModule/memoryAlloc.h>
#include <functional>

namespace IKIGAI::RENDER {
	class FrameBufferInterface;
	class IndexBufferInterface;
	class VertexBufferInterface;
	class ShaderInterface;

	using TextureDeleter = std::function<void(TextureInterface*)>;
	using ModelDeleter = std::function<void(ModelInterface*)>;
	using ShaderDeleter = std::function<void(ShaderInterface*)>;
	using MaterialDeleter = std::function<void(MaterialInterface*)>;

	template <typename T, typename... Args>
	std::shared_ptr<T> AllocateTexture(UTILS::IAllocator* allocator, TextureDeleter customDeleter, Args&&... args) {
		void* rawTex = allocator ? allocator->allocate(sizeof(T)) : ::operator new(sizeof(T));
		auto* texObj = new(rawTex) T(std::forward<Args>(args)...);

		TextureDeleter finalDeleter = [allocator, customDeleter](TextureInterface* p) {
			if (customDeleter) {
				customDeleter(p); // This acts like a generic callback (e.g. unloadResource)
			}
			p->~TextureInterface();
			if (allocator) allocator->deallocate(p);
			else ::operator delete(p);
		};

		return std::shared_ptr<T>(texObj, std::move(finalDeleter));
	}

	template <typename T, typename... Args>
	std::shared_ptr<T> AllocateModel(UTILS::IAllocator* allocator, ModelDeleter customDeleter, Args&&... args) {
		void* raw = allocator ? allocator->allocate(sizeof(T)) : ::operator new(sizeof(T));
		auto* obj = new(raw) T(std::forward<Args>(args)...);

		ModelDeleter finalDeleter = [allocator, customDeleter](ModelInterface* p) {
			if (customDeleter) {
				customDeleter(p);
			}
			p->~ModelInterface();
			if (allocator) allocator->deallocate(p);
			else ::operator delete(p);
		};

		return std::shared_ptr<T>(obj, std::move(finalDeleter));
	}

	template <typename T, typename... Args>
	std::shared_ptr<T> AllocateShader(UTILS::IAllocator* allocator, ShaderDeleter customDeleter, Args&&... args) {
		void* raw = allocator ? allocator->allocate(sizeof(T)) : ::operator new(sizeof(T));
		auto* obj = new(raw) T(std::forward<Args>(args)...);

		ShaderDeleter finalDeleter = [allocator, customDeleter](ShaderInterface* p) {
			if (customDeleter) {
				customDeleter(p);
			}
			p->~ShaderInterface();
			if (allocator) allocator->deallocate(p);
			else ::operator delete(p);
		};
		return std::shared_ptr<T>(obj, std::move(finalDeleter));
	}

	template <typename T, typename... Args>
	std::shared_ptr<T> AllocateMaterial(UTILS::IAllocator* allocator, MaterialDeleter customDeleter, Args&&... args) {
		void* raw = allocator ? allocator->allocate(sizeof(T)) : ::operator new(sizeof(T));
		auto* obj = new(raw) T(std::forward<Args>(args)...);

		MaterialDeleter finalDeleter = [allocator, customDeleter](MaterialInterface* p) {
			if (customDeleter) {
				customDeleter(p);
			}
			p->~MaterialInterface();
			if (allocator) allocator->deallocate(p);
			else ::operator delete(p);
		};

		return std::shared_ptr<T>(obj, std::move(finalDeleter));
	}

	struct RenderSettings {
		enum class Backend { OPENGL, VULKAN, DIRECTX12 };
		Backend backend = Backend::OPENGL;

		template<class Context>
		constexpr static auto serde(Context& context, RenderSettings& value) {
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&RenderSettings::backend, "backend");
		}
	};

	class DriverInterface {
	protected:
		enum class Dirty {
			VIEWPORT = 0,
			SCISSOR,
			PRIMITIVE_MODE,
			RASTERIZATION_MODE,
			VERTEX_BUFFER,
			INDEX_BUFFER,

			DEPTH,
			STENCIL,
			BLENDING,
			CULLING,
			TRIANGULATION_ORDER,

			PIPELINE,

			SIZE
		};

		struct FrameInfo {
			uint64_t mBatchCount = 0;
			uint64_t mInstanceCount = 0;
			uint64_t mPolyCount = 0;
		};

		std::bitset<static_cast<int>(Dirty::SIZE)> mDirty;

	public:
		virtual ~DriverInterface() = default;

		void setDirty(Dirty flag) { mDirty.set(static_cast<int>(flag), true); }
		void clearDirty(Dirty flag) { mDirty.set(static_cast<int>(flag), false); }
		bool isDirty(Dirty flag) const { return mDirty.test(static_cast<int>(flag)); }

		virtual void init() = 0;
		virtual void begin() = 0;
		virtual void end() = 0;

		virtual void submit() = 0;
		virtual void cleanup() = 0;

		virtual void resize(size_t width, size_t height) = 0;

		virtual void setPrimitiveMode(PrimitiveMode topology) = 0;
		virtual void setRasterization(RasterizationMode mode) = 0;
		virtual void setViewport(const Viewport& viewport) = 0;
		virtual void resetViewport() = 0;
		virtual void setScissor(const Scissor& scissor) = 0;
		virtual void resetScissor() = 0;
		virtual void setShader(std::shared_ptr<ShaderInterface> shader) = 0;
		virtual void
			setVertexBuffer(std::shared_ptr<VertexBufferInterface> buffer) = 0;
		virtual void setIndexBuffer(std::shared_ptr<IndexBufferInterface> buffer) = 0;
		virtual void setBlending(const Blending& value) = 0;
		virtual void resetBlending() = 0;
		virtual void setDepth(const Depth& depth) = 0;
		virtual void resetDepth() = 0;
		virtual void setStencil(const Stencil& stencil) = 0;
		virtual void resetStencil() = 0;
		virtual void setCull(CullFace cull_mode) = 0;
		virtual void setTriangleOrientation(TriangleOrientation value) = 0;
		virtual void clear(bool clearColor, bool clearDepth, bool clearStencil) = 0;
		virtual void setClearColor(const MATH::Vector4f& color) = 0;
		virtual void setClearColor(float r, float g, float b, float a) = 0;
		virtual void draw(uint32_t count, uint32_t offset, uint32_t instance) = 0;
		virtual void drawIndexed(uint32_t count, uint32_t offset,
			uint32_t instance) = 0;

		virtual void setTexture(size_t bind,
			std::shared_ptr<TextureInterface> data) = 0;
		virtual void
			setUniformBuffer(size_t bind,
			std::shared_ptr<UniformBufferInterface> data) = 0;
		virtual void
			setStorageBuffer(size_t bind,
			std::shared_ptr<StorageBufferInterface> data) = 0;
		virtual void setTexture(const std::string& name,
			std::shared_ptr<TextureInterface> data) = 0;
		virtual void
			setUniformBuffer(const std::string& name,
			std::shared_ptr<UniformBufferInterface> data) = 0;
		virtual void
			setStorageBuffer(const std::string& name,
			std::shared_ptr<StorageBufferInterface> data) = 0;

		virtual void setMSAA(bool value) = 0;

		virtual void setPushConstant(ShaderType stage, uint32_t offset, uint32_t size, const void* data) {}

		virtual std::shared_ptr<UniformBufferInterface>
			createUniformBuffer(const void* data, size_t size) = 0;
		virtual std::shared_ptr<StorageBufferInterface>
			createStorageBuffer(const void* data, size_t size, size_t stride) = 0;
		virtual std::shared_ptr<TextureInterface> createTexture(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) = 0;
		virtual std::shared_ptr<TextureInterface> createTextureAtlas(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) = 0;
		virtual std::shared_ptr<TextureInterface> createTexture(const TextureResource& res, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) = 0;
		virtual std::shared_ptr<TextureInterface> createTexture(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) = 0;
		virtual std::shared_ptr<TextureInterface> createTextureAtlas(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) = 0;
		virtual std::shared_ptr<TextureInterface> createTexture(const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) = 0;
		virtual std::shared_ptr<ShaderInterface>
			createShader(const std::string& vertexPath,
			const std::string& fragmentPath) = 0;
		virtual std::shared_ptr<ShaderInterface>
			createShader(const ShaderResource& res, UTILS::IAllocator* allocator = nullptr, ShaderDeleter deleter = nullptr) = 0;
		virtual std::shared_ptr<ModelInterface>
			createModel(const std::string& path, UTILS::IAllocator* allocator = nullptr, ModelDeleter deleter = nullptr) = 0;
		virtual std::shared_ptr<MaterialInterface>
			createMaterial(const MaterialResource& res, UTILS::IAllocator* allocator = nullptr, MaterialDeleter deleter = nullptr) = 0;
		virtual std::shared_ptr<FrameBufferInterface> createFrameBuffer(
			const std::vector<std::shared_ptr<TextureInterface>>& textures,
			std::shared_ptr<TextureInterface> depth) = 0;

		virtual void setFrameBuffer(std::shared_ptr<FrameBufferInterface> frameBuffer) = 0;
		virtual void resetFrameBuffer() = 0;

		virtual void draw(const MeshInterface& mesh, PrimitiveMode primitive, uint32_t instances) = 0;

		inline static RenderSettings settings;
	};
} // namespace IKIGAI::RENDER
