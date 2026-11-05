#pragma once
#include "renderModule/frame/gpuCompletion.h"
#include "renderModule/frame/renderOwner.h"
#include "vertexLayout.h"

#include <bitset>
#include <string>

#include "../interface/shaderInterface.h"
#include <memory>

#include "frameBufferInterface.h"
#include "frameGenerationInterface.h"
#include "gpuFeatures.h"
#include "materialInterface.h"
#include "meshInterface.h"
#include "modelInterface.h"
#include "rayTracingInterface.h"
#include "resourceStruct.h"
#include "storageBufferInterface.h"
#include "textureInterface.h"
#include "upscalerInterface.h"
#include <functional>
#include <serdepp/attribute/default.hpp>
#include <utilsModule/memory/memory.h>

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
	return MEMORY::MakeSharedWithDeleter<T, MEMORY::Category::Texture>(allocator, std::move(customDeleter), std::forward<Args>(args)...);
}

template <typename T, typename... Args> std::shared_ptr<T> AllocateModel(UTILS::IAllocator* allocator, ModelDeleter customDeleter, Args&&... args) {
	return MEMORY::MakeSharedWithDeleter<T, MEMORY::Category::Model>(allocator, std::move(customDeleter), std::forward<Args>(args)...);
}

template <typename T, typename... Args> std::shared_ptr<T> AllocateShader(UTILS::IAllocator* allocator, ShaderDeleter customDeleter, Args&&... args) {
	return MEMORY::MakeSharedWithDeleter<T, MEMORY::Category::Shader>(allocator, std::move(customDeleter), std::forward<Args>(args)...);
}

template <typename T, typename... Args>
std::shared_ptr<T> AllocateMaterial(UTILS::IAllocator* allocator, MaterialDeleter customDeleter, Args&&... args) {
	return MEMORY::MakeSharedWithDeleter<T, MEMORY::Category::Material>(allocator, std::move(customDeleter), std::forward<Args>(args)...);
}

struct RenderSettings {
	enum class Backend { OPENGL, VULKAN, DIRECTX12, METAL };
	// DEFAULT keeps each backend's historical behaviour (Vulkan FIFO, DX12
	// frameGeneration.vsync). FIFO forces vsync; MAILBOX/IMMEDIATE disable it
	// (DX12 presents with sync interval 0 for both). Unsupported Vulkan modes fall back to FIFO.
	enum class PresentMode { DEFAULT, FIFO, MAILBOX, IMMEDIATE };
	enum class Execution { IMMEDIATE, DEFERRED, THREADED };
	Backend backend = Backend::OPENGL;
	PresentMode presentMode = PresentMode::DEFAULT;
	Execution execution = Execution::IMMEDIATE;
	std::string pipeline;
	UpscalerSettings upscaler;
	FrameGenerationSettings frameGeneration;

	template <class Context> constexpr static auto serde(Context& context, RenderSettings& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&RenderSettings::backend, "backend", default_ { Backend::OPENGL })
			.field(&RenderSettings::pipeline, "pipeline", default_ { std::string {} })
			.field(&RenderSettings::execution, "execution", default_ { Execution::IMMEDIATE })
			.field(&RenderSettings::upscaler, "upscaler", default_ { UpscalerSettings {} })
			.field(&RenderSettings::frameGeneration, "frameGeneration", default_ { FrameGenerationSettings {} })
			.field(&RenderSettings::presentMode, "presentMode", default_ { PresentMode::DEFAULT });
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
	std::shared_ptr<GpuCompletionSource> completionSource() const {
		return mCompletion;
	}

public:
	virtual ~DriverInterface() = default;
	void requireOwnerThread() const;
	const GpuCompletionSource& completion() const noexcept {
		return *mCompletion;
	}
	virtual void waitForCompletion(GpuCompletionToken token);
	void setAccessBoundary(std::function<void()> boundary) {
		mAccessBoundary = std::move(boundary);
	}
	void prepareAccess();

	void setDirty(Dirty flag) {
		mDirty.set(static_cast<int>(flag), true);
	}
	void clearDirty(Dirty flag) {
		mDirty.set(static_cast<int>(flag), false);
	}
	bool isDirty(Dirty flag) const {
		return mDirty.test(static_cast<int>(flag));
	}

	virtual void init() = 0;
	virtual void begin() = 0;
	virtual void end() = 0;
	// Immediate APIs always have work that can be drained; command-list backends report their recording state.
	virtual bool hasOpenRecording() const noexcept {
		return true;
	}

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
	virtual void setVertexBuffer(std::shared_ptr<VertexBufferInterface> buffer) = 0;
	virtual void setIndexBuffer(std::shared_ptr<IndexBufferInterface> buffer) = 0;
	// A null layout preserves the historical Vertex/MetalMeshVertex path.
	std::optional<VertexLayout> mVertexLayout;
	std::vector<std::pair<uint32_t, std::shared_ptr<VertexBufferInterface>>> mVertexStreams;
	void resetVertexStreams() {
		if (mVertexLayout) {
			setDirty(Dirty::PIPELINE);
		}
		mVertexLayout.reset();
		mVertexStreams.clear();
	}
	void setVertexStreams(const VertexLayout& layout, const std::vector<std::pair<uint32_t, std::shared_ptr<VertexBufferInterface>>>& streams) {
		layout.validate();
		const auto oldKey = mVertexLayout ? mVertexLayout->key() : "legacy";
		mVertexLayout = layout;
		mVertexStreams = streams;
		setDirty(Dirty::VERTEX_BUFFER);
		if (oldKey != layout.key()) {
			setDirty(Dirty::PIPELINE);
		}
	}
	std::string vertexLayoutKey() const {
		return mVertexLayout ? mVertexLayout->key() : "legacy";
	}
	void validateVertexInputs(const ShaderInterface& shader) const;
	void validateVertexDraw(const DrawArguments& args) const;
	virtual void draw(const DrawArguments& args) {
		if (args.firstInstance || args.baseVertex) {
			throw std::runtime_error("Base instance/vertex is unavailable on this backend");
		}
		if (args.indexed) {
			drawIndexed(args.count, args.first, args.instanceCount);
		}
		else {
			draw(args.count, args.first, args.instanceCount);
		}
	}
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
	virtual void dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;
	virtual void renderGraphBarrier(bool uav);
	virtual BindlessCapabilities bindlessCapabilities() const {
		return {};
	}
	virtual uint32_t maxFragmentSampledTextures() const {
		return 16;
	}
	virtual bool supportsVertexPulling() const {
		return false;
	}
	std::map<uint32_t, BindlessResourceTable> mBindlessTables;
	std::vector<BindlessResourceEntry> mBindlessFrameResources;
	BindlessResourceHandle registerBindlessTexture(uint32_t binding, std::shared_ptr<TextureInterface> texture, uint32_t capacity = 64) {
		const auto caps = bindlessCapabilities();
		if (!caps.sampledTextures || capacity > caps.maxSampledTextures) {
			throw std::runtime_error("Bindless texture table is unsupported or exceeds device limit");
		}
		return mBindlessTables[binding].add(binding, { std::move(texture), {} }, capacity);
	}
	BindlessResourceHandle registerBindlessBuffer(uint32_t binding, std::shared_ptr<StorageBufferInterface> buffer, uint32_t capacity = 64) {
		const auto caps = bindlessCapabilities();
		if (!caps.storageBuffers || capacity > caps.maxStorageBuffers) {
			throw std::runtime_error("Bindless buffer table is unsupported or exceeds device limit");
		}
		return mBindlessTables[binding].add(binding, { {}, std::move(buffer) }, capacity);
	}
	void releaseBindlessResource(BindlessResourceHandle handle) {
		mBindlessTables.at(handle.binding).release(handle);
	}
	const BindlessResourceTable& bindlessTable(const ShaderReflection::Uniform& uniform, uint32_t count) const {
		const auto it = mBindlessTables.find(static_cast<uint32_t>(uniform.mBind));
		if (it == mBindlessTables.end() || it->second.capacity != count) {
			throw std::runtime_error("Missing or incompatible bindless table: " + uniform.mName);
		}
		const auto& entry = it->second.get(0);
		if (uniform.mType == ShaderReflection::UniformType::STORAGE_BUFFER) {
			if (!entry.buffer || !entry.buffer->getSizeByte()) {
				throw std::runtime_error("Bindless table needs valid buffers: " + uniform.mName);
			}
		}
		else {
			const auto expected = uniform.mType == ShaderReflection::UniformType::SAMPLER_CUBE ? TextureType::TEXTURE_CUBE
				: uniform.mType == ShaderReflection::UniformType::SAMPLER_3D				   ? TextureType::TEXTURE_3D
				: uniform.mType == ShaderReflection::UniformType::SAMPLER_2D_ARRAY			   ? TextureType::TEXTURE_2D_ARRAY
																							   : TextureType::TEXTURE_2D;
			if (!entry.texture
				|| (entry.texture->getType() != expected
					&& !(expected == TextureType::TEXTURE_2D && entry.texture->getType() == TextureType::DEPTH))) {
				throw std::runtime_error("Bindless texture view type does not match shader: " + uniform.mName);
			}
		}
		return it->second;
	}
	virtual std::unique_ptr<UpscalerInterface> createUpscaler(UpscalerType) {
		throw UnsupportedUpscaler("DLSS/FSR are only available on enabled Vulkan/DX12 builds");
	}
	std::unique_ptr<FrameGenerationInterface> mFrameGeneration;
	FrameGenerationStatus mFrameGenerationStatus;
	FrameGenerationSettings mFrameGenerationSettings;
	bool mFrameGenerationSettingsLoaded = false, mFrameGenerationFailed = false, mPresentationUIRendered = false;
	virtual std::unique_ptr<FrameGenerationInterface> createFrameGeneration(FrameGenerationType) {
		throw UnsupportedFrameGeneration("Frame generation SDK is not compiled for this backend");
	}
	void prepareFrameGeneration(bool pipelineEnabled, MATH::Vector2i render, MATH::Vector2i display);
	void dispatchFrameGeneration(const FrameGenerationInputs&);
	virtual void renderPresentationUI(std::shared_ptr<FrameBufferInterface>) { }
	uint64_t mRealFrameId = UINT64_MAX;
	virtual void setTemporalCamera(const FrameGenerationInputs&) { }
	virtual void latencyFrameBegin() {
		++mRealFrameId;
	}
	virtual void latencySimulationEnd() { }
	void stopPresentation() {
		mFrameGeneration.reset();
		mFrameGenerationStatus = {};
	}
	virtual void latencySubmitBegin() { }
	virtual void latencySubmitEnd() { }
	virtual RayTracingCapabilities rayTracingCapabilities() const {
		return {};
	}
	// GPU-driven rendering contract (gpuFeatures.h). Vulkan and DirectX 12 only.
	// Commands are recorded into the current frame; the driver inserts the barriers
	// needed between compute writes and indirect/vertex/index/transfer reads.
	virtual GpuFeatures gpuFeatures() const {
		return {};
	}
	// count elements of stride bytes, matching createStorageBuffer(data, size, stride).
	virtual std::shared_ptr<StorageBufferInterface> createStorageBuffer(const void*, size_t, size_t, BufferUsage) {
		throw UnsupportedGpuFeature("Storage buffer usage flags are unavailable on this backend");
	}
	virtual void fillBuffer(const std::shared_ptr<StorageBufferInterface>&, size_t, size_t, uint32_t) {
		throw UnsupportedGpuFeature("Buffer fill is unavailable on this backend");
	}
	virtual void copyBuffer(const std::shared_ptr<StorageBufferInterface>&, size_t, const std::shared_ptr<StorageBufferInterface>&, size_t, size_t) {
		throw UnsupportedGpuFeature("Buffer copy is unavailable on this backend");
	}
	// Synchronous: waits for all recorded GPU work. Tests and debug tools only.
	virtual std::vector<uint8_t> readBuffer(const std::shared_ptr<StorageBufferInterface>&, size_t, size_t) {
		throw UnsupportedGpuFeature("Buffer readback is unavailable on this backend");
	}
	// Non-blocking readback (per-frame statistics): records a copy of the range into driver-owned host memory at this
	// point of the frame and returns a ticket. collectBufferReadback returns true with the bytes once the GPU finished
	// that work (the ticket is consumed), false while it is pending or unknown. Never waits for the GPU.
	virtual uint64_t requestBufferReadback(const std::shared_ptr<StorageBufferInterface>&, size_t, size_t) {
		throw UnsupportedGpuFeature("Asynchronous buffer readback is unavailable on this backend");
	}
	virtual bool collectBufferReadback(uint64_t, std::vector<uint8_t>&) {
		return false;
	}
	// Uses the currently bound shader, vertex and index buffers like drawIndexed.
	virtual void drawIndexedIndirect(const std::shared_ptr<StorageBufferInterface>&, size_t, uint32_t, uint32_t) {
		throw UnsupportedGpuFeature("Indirect draw is unavailable on this backend");
	}
	virtual void drawIndexedIndirectCount(
		const std::shared_ptr<StorageBufferInterface>&, size_t, const std::shared_ptr<StorageBufferInterface>&, size_t, uint32_t, uint32_t) {
		throw UnsupportedGpuFeature("Indirect count draw is unavailable on this backend");
	}
	virtual void dispatchIndirect(const std::shared_ptr<StorageBufferInterface>&, size_t) {
		throw UnsupportedGpuFeature("Indirect dispatch is unavailable on this backend");
	}
	// Binds one mip/layer of a texture as a storage image for the next dispatch.
	virtual void setStorageImage(size_t, std::shared_ptr<TextureInterface>, uint32_t, uint32_t) {
		throw UnsupportedGpuFeature("Storage image mip views are unavailable on this backend");
	}
	// Timestamp queries for GpuStageProfiler. Indices are in [0, timestampQueryCapacity()).
	virtual uint32_t timestampQueryCapacity() const {
		return 0;
	}
	virtual void writeTimestamp(uint32_t) {
		throw UnsupportedGpuFeature("Timestamp queries are unavailable on this backend");
	}
	// Returns false while results are not yet available. Values are raw ticks.
	virtual bool readTimestamps(uint32_t, uint32_t, std::vector<uint64_t>&) {
		return false;
	}
	virtual std::shared_ptr<VertexBufferInterface> createRayTracingVertexBuffer(const void*, size_t, size_t) {
		throw UnsupportedRayTracing("RT vertex buffers are unavailable on this backend");
	}
	virtual std::shared_ptr<RayTracingProgram> createRayTracingProgram(const RayTracingProgramResource&) {
		throw std::runtime_error("Ray tracing pipelines are unavailable on this backend");
	}
	// Reflected compute programs use the same descriptor snapshot format.
	// This entry point does not require ray-query device capabilities.
	virtual std::shared_ptr<RayTracingProgram> createComputeProgram(const RayTracingProgramResource&) {
		throw std::runtime_error("Reflected compute programs are unavailable on this backend");
	}
	virtual void dispatchComputeProgram(const RayTracingDispatch&) {
		throw std::runtime_error("Reflected compute dispatch is unavailable on this backend");
	}
	virtual std::shared_ptr<AccelerationStructure> createBottomLevelAS(const std::vector<RayTracingGeometry>&, bool allowUpdate = false) {
		throw std::runtime_error("Acceleration structures are unavailable on this backend");
	}
	virtual std::shared_ptr<AccelerationStructure> createTopLevelAS(const std::vector<RayTracingInstance>&, bool allowUpdate = false) {
		throw std::runtime_error("Acceleration structures are unavailable on this backend");
	}
	// Refit preserves topology. Vertex buffers must keep their allocation,
	// indices and active primitive set; changing these requires a rebuild.
	virtual void refitBottomLevelAS(const std::shared_ptr<AccelerationStructure>&) {
		throw std::runtime_error("Acceleration structure refit is unavailable on this backend");
	}
	virtual void refitTopLevelAS(const std::shared_ptr<AccelerationStructure>&, const std::vector<RayTracingInstance>&) {
		throw std::runtime_error("Acceleration structure refit is unavailable on this backend");
	}
	virtual void traceRays(const RayTracingDispatch&) {
		throw std::runtime_error("Ray tracing dispatch is unavailable on this backend");
	}
	virtual void dispatchRayQueries(const RayTracingDispatch&) {
		throw std::runtime_error("Ray query dispatch is unavailable on this backend");
	}
	// Synchronous diagnostic readback of one mip/face (or one 3D slice).
	virtual std::vector<float> readTexture(std::shared_ptr<TextureInterface> texture, unsigned mip, unsigned layer) {
		throw std::runtime_error("Float texture readback is unavailable on this backend");
	}
	virtual void draw(uint32_t count, uint32_t offset, uint32_t instance) = 0;
	virtual void drawIndexed(uint32_t count, uint32_t offset, uint32_t instance) = 0;

	virtual void setTexture(size_t bind, std::shared_ptr<TextureInterface> data) = 0;
	virtual void setUniformBuffer(size_t bind, std::shared_ptr<UniformBufferInterface> data) = 0;
	virtual void setStorageBuffer(size_t bind, std::shared_ptr<StorageBufferInterface> data) = 0;
	virtual void setTexture(const std::string& name, std::shared_ptr<TextureInterface> data) = 0;
	virtual void setUniformBuffer(const std::string& name, std::shared_ptr<UniformBufferInterface> data) = 0;
	virtual void setStorageBuffer(const std::string& name, std::shared_ptr<StorageBufferInterface> data) = 0;

	virtual void setMSAA(bool value) = 0;

	virtual void setPushConstant(ShaderType stage, uint32_t offset, uint32_t size, const void* data) { }

	virtual std::shared_ptr<UniformBufferInterface> createUniformBuffer(const void* data, size_t size) = 0;
	virtual std::shared_ptr<StorageBufferInterface> createStorageBuffer(const void* data, size_t size, size_t stride) = 0;
	virtual std::shared_ptr<TextureInterface> createTexture(
		const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr)
		= 0;
	virtual std::shared_ptr<TextureInterface> createTextureAtlas(
		const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr)
		= 0;
	virtual std::shared_ptr<TextureInterface> createTexture(
		const TextureResource& res, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr)
		= 0;
	virtual std::shared_ptr<TextureInterface> createTexture(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData,
		UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr)
		= 0;
	virtual std::shared_ptr<TextureInterface> createTextureAtlas(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData,
		UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr)
		= 0;
	virtual std::shared_ptr<TextureInterface> createTexture(const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap,
		UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr)
		= 0;
	virtual std::shared_ptr<ShaderInterface> createShader(const std::string& vertexPath, const std::string& fragmentPath) = 0;
	virtual std::shared_ptr<ShaderInterface> createShader(
		const ShaderResource& res, UTILS::IAllocator* allocator = nullptr, ShaderDeleter deleter = nullptr)
		= 0;
	virtual std::shared_ptr<ModelInterface> createModel(
		const std::string& path, UTILS::IAllocator* allocator = nullptr, ModelDeleter deleter = nullptr)
		= 0;
	virtual std::shared_ptr<MaterialInterface> createMaterial(
		const MaterialResource& res, UTILS::IAllocator* allocator = nullptr, MaterialDeleter deleter = nullptr)
		= 0;
	virtual std::shared_ptr<FrameBufferInterface> createFrameBuffer(
		const std::vector<std::shared_ptr<TextureInterface>>& textures, std::shared_ptr<TextureInterface> depth, unsigned mip = 0, unsigned layer = 0)
		= 0;

	virtual void setFrameBuffer(std::shared_ptr<FrameBufferInterface> frameBuffer) = 0;
	virtual void resetFrameBuffer() = 0;

	virtual void draw(const MeshInterface& mesh, PrimitiveMode primitive, uint32_t instances) = 0;

	virtual void renderImGui() { }

	static DriverInterface* Get();
	static void SetActive(DriverInterface* driver) {
		sActive = driver;
	}

	inline static RenderSettings settings;

private:
	friend class Renderer;
	friend class RenderResourceOwner;
	// Vulkan and DX12 split the present path at the GPU fence. Window calls and
	// post-fence housekeeping run on the producer.
	virtual void presentFrame();
	virtual void waitForPresentedFrame();
	virtual void finishPresent();
	void withReplayAccess(const std::function<void()>& work);
	static std::thread::id resourceOwnerThread() noexcept;
	static thread_local DriverInterface* sReplayDriver;
	RenderOwner mOwner {};
	std::shared_ptr<GpuCompletionSource> mCompletion = std::make_shared<GpuCompletionSource>();
	std::function<void()> mAccessBoundary {};
	inline static DriverInterface* sActive = nullptr;
};
} // namespace IKIGAI::RENDER
