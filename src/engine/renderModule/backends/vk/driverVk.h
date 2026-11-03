#pragma once
#include <bitset>
#include <memory>
#include <unordered_set>
#include <variant>
#include <vector>

#include "indexBufferVk.h"
#include "mathModule/math.h"
#include "renderModule/backends/interface/storageBufferInterface.h"
#include "renderModule/backends/interface/textureInterface.h"
#include "shaderVk.h"
#include "vertexBufferVk.h"

#ifdef VULKAN_BACKEND
#include "../interface/driverInterface.h"
#include "swapChainHandler.h"
#include "vmaVk.h"
#include <assimp/scene.h>
#include <queue>
#include <volk.h>

struct ImDrawData;

namespace IKIGAI::RENDER {
struct StreamlineCallbacks;
class StorageBufferVk;
class UniformBufferVk;
class MeshInterface;
class ShaderVk;
class FrameBufferVk;
class CommandBuffer;

class DriverVk : public DriverInterface {
	friend class FsrFrameGenerationVk;
	friend class StreamlineFrameGenerationVk;

public:
	struct VolkInitializer {
		std::shared_ptr<void> Runtime;
		VolkInitializer();
	};
	VolkInitializer mVolkInit;
	vk::raii::Context mContext;
	vk::raii::Instance mInstance = nullptr;
	vk::raii::PhysicalDevice mPhysicalDevice = nullptr;
	vk::raii::Queue mQueue = nullptr;
	vk::raii::Device mDevice = nullptr;
	VmaAllocator mAllocator = nullptr;
#if IKIGAI_ENABLE_MEMORY_TRACKING
	std::shared_ptr<PROFILER::GpuMemorySampler> mMemorySampler;
#endif
	RayTracingCapabilities mRayTracingCapabilities;
	std::shared_ptr<void> mDlssRuntime;
	std::string mDlssUnavailable;
	std::unique_ptr<UpscalerInterface> createUpscaler(UpscalerType) override;
	std::unique_ptr<FrameGenerationInterface> createFrameGeneration(FrameGenerationType) override;
	void renderPresentationUI(std::shared_ptr<FrameBufferInterface>) override;
	StreamlineCallbacks streamlineCallbacks();
	void streamlinePresenter(bool enable, bool commandOpen = true);
	void setTemporalCamera(const FrameGenerationInputs&) override;
	void latencyFrameBegin() override;
	void latencySimulationEnd() override;
	void latencySubmitBegin() override;
	void latencySubmitEnd() override;
	void bindStreamlineDevice();
	void configureStreamlineExtensions(std::vector<const char*>&, bool instance);
	std::array<VkQueue, 3> mPresentationQueues {};
	uint32_t mOpticalFlowFamily = UINT32_MAX;
	void configureUpscalerExtensions(std::vector<const char*>& extensions);
	void configureUpscalerInstanceExtensions(std::vector<const char*>& extensions);
	VkPhysicalDeviceAccelerationStructureFeaturesKHR mASFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR mRTFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
	VkPhysicalDeviceRayQueryFeaturesKHR mRQFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
	VkPhysicalDeviceVulkan12Features mRTCoreFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR mRTProperties { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR };
	VkPhysicalDeviceAccelerationStructurePropertiesKHR mASProperties { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
	std::vector<std::shared_ptr<void>> mRayTracingFrameResources;
	std::map<std::string, std::weak_ptr<RayTracingProgram>> mRayTracingPrograms;
	void configureRayTracing(std::vector<const char*>& extensions);
	RayTracingCapabilities rayTracingCapabilities() const override {
		return mRayTracingCapabilities;
	}
	std::shared_ptr<VertexBufferInterface> createRayTracingVertexBuffer(const void*, size_t, size_t) override;
	std::shared_ptr<RayTracingProgram> createRayTracingProgram(const RayTracingProgramResource&) override;
	std::shared_ptr<RayTracingProgram> createComputeProgram(const RayTracingProgramResource&) override;
	void dispatchComputeProgram(const RayTracingDispatch&) override;
	std::shared_ptr<RayTracingProgram> createRayTracingProgramImpl(const RayTracingProgramResource&, bool requireRayQuery);
	std::shared_ptr<AccelerationStructure> createBottomLevelAS(const std::vector<RayTracingGeometry>&, bool allowUpdate = false) override;
	std::shared_ptr<AccelerationStructure> createTopLevelAS(const std::vector<RayTracingInstance>&, bool allowUpdate = false) override;
	void refitBottomLevelAS(const std::shared_ptr<AccelerationStructure>&) override;
	void refitTopLevelAS(const std::shared_ptr<AccelerationStructure>&, const std::vector<RayTracingInstance>&) override;
	void traceRays(const RayTracingDispatch&) override;
	void dispatchRayQueries(const RayTracingDispatch&) override;
	void executeRayProgram(const RayTracingDispatch&, bool rayQuery);
	// Timestamp queries for GpuStageProfiler (timestampQueriesVk.cpp).
	vk::raii::QueryPool mTimestampPool = nullptr;
	uint32_t mTimestampCapacity = 0;
	double mTimestampPeriodNanoseconds = 0.0;
	uint64_t mTimestampMask = 0;
	bool mTimestampPoolNeedsReset = false;
	void createTimestampQueries();
	void resetTimestampPoolIfNeeded();
	uint32_t timestampQueryCapacity() const override;
	void writeTimestamp(uint32_t index) override;
	bool readTimestamps(uint32_t first, uint32_t count, std::vector<uint64_t>& ticks) override;
	uint32_t mQueueFamilyIndex = -1;
	vk::SurfaceFormatKHR mSurfaceFormat;

	vk::raii::CommandPool mCommandPool = nullptr;
	// Freed at the start of begin(), after the previous frame's fence has signaled.
	vk::raii::DescriptorPool mDescriptorPool = nullptr;
	std::vector<vk::raii::DescriptorPool> mExtraDescriptorPools;
	size_t mDescriptorPoolIndex = 0;
	vk::raii::DescriptorPool createDrawDescriptorPool();
	std::vector<vk::raii::DescriptorSet> mFrameDescriptorSets;

	constexpr static vk::Format DefaultDepthStencilFormat = vk::Format::eD32SfloatS8Uint;

	bool working = false;
	bool mShuttingDown = false;

	bool render_pass_active = false;

	using VulkanObject = std::variant<VmaBuffer, VmaImage, vk::raii::ImageView, vk::raii::Sampler, std::shared_ptr<void>, vk::raii::Pipeline,
		vk::raii::AccelerationStructureKHR>;

	struct Frame {
		// Declared first so it is destroyed last (after framebuffer textures that may queue into it).
		std::vector<VulkanObject> destroyDeferred;
		vk::raii::Fence fence = nullptr;
		std::shared_ptr<FrameBufferVk> mFrameBuffer;
		vk::raii::Semaphore mImageAcquiredSemaphore = nullptr;
		vk::raii::Semaphore mRenderCompleteSemaphore = nullptr;
		vk::raii::CommandBuffer mCommandBuffer = nullptr;
	};

	struct SwapchainContextVk {
		vk::raii::SurfaceKHR surface = nullptr;
		vk::raii::SwapchainKHR swapchain = nullptr;
		VkSwapchainKHR sdkSwapchain = VK_NULL_HANDLE;
		std::function<VkResult(VkSemaphore, uint32_t*)> sdkAcquire;
		std::function<VkResult(const VkPresentInfoKHR*)> sdkPresent;
		std::function<void(uint32_t, uint32_t)> sdkResize;
		std::function<void()> sdkDestroy;
		std::function<void(VkSurfaceKHR)> sdkDestroySurface;
		bool streamline = false;
		vk::SwapchainCreateInfoKHR lastCreateInfo {};
		std::vector<Frame> frames;
		uint32_t width = 0;
		uint32_t height = 0;
	};

	std::unordered_map<unsigned int, SwapchainContextVk> mSwapchains;
	unsigned int mCurrentWindowID = 0;

	uint32_t mSemaphoreIndex = 0;
	uint32_t mFrameIndex = 0;
	uint32_t mWidth = 0;
	uint32_t mHeight = 0;

	struct State {
		std::shared_ptr<ShaderVk> mShader;
		std::shared_ptr<FrameBufferVk> mFrameBuffer;

		// std::vector<vk::Format> color_attachment_formats;
		// std::optional<vk::Format> depth_stencil_format;
		// std::vector<InputLayout> input_layouts;

		std::string getName();
	};

	State mCurrentState;
	std::map<std::string, vk::raii::Pipeline> mStates;
#if IKIGAI_ENABLE_MEMORY_TRACKING
	std::map<std::uint64_t, PROFILER::GpuAllocationToken> mGpuPipelines;
#endif

	VkDescriptorPool mImguiPool;

	// Frame& getCurrentFrame() { return frames.at(frame_index); }

	// std::unordered_map<uint32_t, TextureVK*> textures;
	// std::unordered_map<uint32_t, UniformBufferVK*> uniform_buffers;
	// std::unordered_map<uint32_t, StorageBufferVK*> storage_buffers;
	// std::unordered_map<uint32_t, TopLevelAccelerationStructureVK*> top_level_acceleration_structures;

	// std::unordered_map<PipelineStateVK, vk::raii::Pipeline> pipeline_states;

	// RaytracingPipelineStateVK raytracing_pipeline_state;
	// std::unordered_map<RaytracingPipelineStateVK, vk::raii::Pipeline> raytracing_pipeline_states;

	// SamplerStateVK sampler_state;
	// std::unordered_map<SamplerStateVK, vk::raii::Sampler> sampler_states;

	// std::vector<RenderTargetVK*> render_targets;

	// PipelineStateVK pipeline_state;
	// std::optional<Scissor> scissor;
	bool mMSAA = false;
	// std::optional<DepthMode> depth_mode = DepthMode();
	// std::optional<StencilMode> stencil_mode;
	// CullMode cull_mode = CullMode::None;
	// FrontFace front_face = FrontFace::Clockwise;
	// Topology topology = Topology::TriangleList;
	// std::vector<VertexBufferVK*> vertex_buffers; // TODO: store pointer and count, not std::vector
	// IndexBufferVK* index_buffer = nullptr;
	// std::optional<BlendMode> blend_mode;

	// bool pipeline_state_dirty = true;
	// bool scissor_dirty = true;
	// bool viewport_dirty = true;
	// bool depth_mode_dirty = true;
	// bool stencil_mode_dirty = true;
	// bool cull_mode_dirty = true;
	// bool front_face_dirty = true;
	// bool topology_dirty = true;
	// bool vertex_buffers_dirty = true;
	// bool index_buffer_dirty = true;
	// bool blend_mode_dirty = true;

	std::unordered_set<uint32_t> graphics_pipeline_ignore_bindings;
	// Name of the pipeline bound in the current command buffer (shader id + framebuffer id).
	// Used to rebind the pipeline when shader/framebuffer changed between draws.
	std::string mBoundPipelineName;
	//
	// uint32_t getBackbufferWidth();
	// uint32_t getBackbufferHeight();
	// vk::Format getBackbufferFormat();
	//
	// bool render_pass_active = false;
	//
	vk::PipelineStageFlags2 mCurrentMemoryStage = vk::PipelineStageFlagBits2::eTransfer;
	//
	// std::unordered_set<ObjectVK*> objects;

	std::map<size_t, std::shared_ptr<UniformBufferVk>> mUniformBuffers;
	std::map<size_t, std::shared_ptr<TextureVk>> mTextures;
	std::map<size_t, std::shared_ptr<StorageBufferVk>> mStorageBuffers;

	DriverVk();
	~DriverVk() override;

	void begin() override;
	void end() override;
	vk::raii::Pipeline createState(const State& pipeline_state);
	void EnsureVertexBuffers(vk::raii::CommandBuffer& cmdlist);
	void EnsureIndexBuffer(vk::raii::CommandBuffer& cmdlist);
	void EnsureTopology(vk::raii::CommandBuffer& cmdlist);
	void EnsureViewport(vk::raii::CommandBuffer& cmdlist);
	void EnsureScissor(vk::raii::CommandBuffer& cmdlist);
	void EnsureCullMode(vk::raii::CommandBuffer& cmdlist);
	void EnsureFrontFace(vk::raii::CommandBuffer& cmdlist);
	void EnsureBlendMode(vk::raii::CommandBuffer& cmdlist);
	void EnsureDepthMode(vk::raii::CommandBuffer& cmdlist);
	void EnsureStencilMode(vk::raii::CommandBuffer& cmdlist);
	void EnsureGraphicsPipelineState(vk::raii::CommandBuffer& cmdlist);
	void EnsureGraphicsDescriptors(vk::raii::CommandBuffer& cmdlist);
	void EnsureGraphicsState(bool draw_indexed);

	SwapchainContextVk& getCurrentSwapchainContext() {
		return mSwapchains[mCurrentWindowID];
	}
	Frame& getCurrentFrame() {
		return getCurrentSwapchainContext().frames.at(mFrameIndex);
	}

	void activateRenderPass();
	void deactivateRenderPass();

	void destroyDeferred(VulkanObject&& object);
	std::vector<VulkanObject>& getDestroyDeferredObjects();
	void EnsureMemoryState(const vk::raii::CommandBuffer& cmdbuf, vk::PipelineStageFlags2 stage);
	void PushDescriptorBuffer(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
		const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding, vk::DescriptorType type, vk::Buffer buffer);
	void PushDescriptorTexture(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
		const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding);
	void PushDescriptorUniformBuffer(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
		const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding);
	void PushDescriptorStorageImage(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
		const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding);
	void PushDescriptorStorageBuffer(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
		const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding);
	void PushDescriptors(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point, const vk::raii::PipelineLayout& pipeline_layout,
		const std::vector<vk::DescriptorSetLayoutBinding>& required_descriptor_bindings, const std::unordered_set<uint32_t>& ignore_bindings);
	void PushDescriptorAccelerationStructure(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
		const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding);
	void resize(size_t width, size_t height) override;
	void setPrimitiveMode(PrimitiveMode topology) override;
	RasterizationMode mRasterizationMode = RasterizationMode::FILL;
	void setRasterization(RasterizationMode mode) override;
	void setViewport(const Viewport& viewport) override;
	void setScissor(const Scissor& scissor) override;
	void setTexture(size_t binding, std::shared_ptr<TextureInterface> handle) override;
	void setShader(std::shared_ptr<ShaderInterface> shader) override;
	void setVertexBuffer(std::shared_ptr<VertexBufferInterface> buffer) override;
	void setIndexBuffer(std::shared_ptr<IndexBufferInterface> buffer) override;
	void setUniformBuffer(size_t binding, std::shared_ptr<UniformBufferInterface> handle) override;
	void setStorageBuffer(size_t binding, std::shared_ptr<StorageBufferInterface> handle) override;
	void setTexture(const std::string& name, std::shared_ptr<TextureInterface> data) override;
	void setUniformBuffer(const std::string& name, std::shared_ptr<UniformBufferInterface> data) override;
	void setStorageBuffer(const std::string& name, std::shared_ptr<StorageBufferInterface> data) override;
	void setMSAA(bool value) override;
	void setPushConstant(ShaderType stage, uint32_t offset, uint32_t size, const void* data) override;

	std::shared_ptr<TextureInterface> createTexture(
		const std::string& path, bool generateMipmap = true, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) override;
	std::shared_ptr<TextureInterface> createTextureAtlas(
		const std::string& path, bool generateMipmap = true, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) override;
	std::shared_ptr<TextureInterface> createTexture(
		const TextureResource& res, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) override;
	std::shared_ptr<TextureInterface> createTexture(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData,
		UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) override;
	std::shared_ptr<TextureInterface> createTextureAtlas(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData,
		UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) override;
	std::shared_ptr<TextureInterface> createTexture(const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap,
		UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) override;

	std::shared_ptr<ShaderInterface> createShader(const std::string& vertexPath, const std::string& fragmentPath) override;
	std::shared_ptr<ShaderInterface> createShader(
		const ShaderResource& res, UTILS::IAllocator* allocator = nullptr, ShaderDeleter deleter = nullptr) override;

	std::shared_ptr<ModelInterface> createModel(
		const std::string& path, UTILS::IAllocator* allocator = nullptr, ModelDeleter deleter = nullptr) override;

	std::shared_ptr<MaterialInterface> createMaterial(
		const MaterialResource& res, UTILS::IAllocator* allocator = nullptr, MaterialDeleter deleter = nullptr) override;

	std::shared_ptr<FrameBufferInterface> createFrameBuffer(const std::vector<std::shared_ptr<TextureInterface>>& textures,
		std::shared_ptr<TextureInterface> depth, unsigned mip = 0, unsigned layer = 0) override;

	void setBlending(const Blending& value) override;
	void setDepth(const Depth& depth) override;
	void setStencil(const Stencil& stencil) override;
	void setCull(CullFace cull_mode) override;
	void setTriangleOrientation(TriangleOrientation value) override;
	void draw(uint32_t vertex_count, uint32_t vertex_offset, uint32_t instance_count) override;
	std::vector<float> readTexture(std::shared_ptr<TextureInterface> texture, unsigned mip, unsigned layer) override;
	void dispatch(uint32_t x, uint32_t y, uint32_t z) override;
	// Texture infrastructure for GI/HZB (M1-4, M1-8); implemented in textureFeaturesVk.cpp.
	void setStorageImage(size_t binding, std::shared_ptr<TextureInterface> texture, uint32_t mip, uint32_t layer) override;
	// Sets StorageImageMipViews, HalfFloatTextures and SamplerMinMaxReduction.
	void fillTextureGpuFeatures(GpuFeatures& features) const;
	struct StorageImageBinding {
		std::shared_ptr<TextureVk> Texture;
		uint32_t Mip = 0;
		uint32_t Layer = 0;
	};
	// Per-mip/per-layer storage image overrides; setTexture on the same binding removes the override.
	std::map<size_t, StorageImageBinding> mStorageImageBindings;
	BindlessCapabilities mBindlessCapabilities;
	BindlessCapabilities bindlessCapabilities() const override;
	uint32_t maxFragmentSampledTextures() const override;
	bool supportsVertexPulling() const override {
		return true;
	}
	void draw(const DrawArguments& args) override;
	void drawIndexed(uint32_t index_count, uint32_t index_offset, uint32_t instance_count) override;
	void draw(const MeshInterface& mesh, PrimitiveMode primitive, uint32_t instances) override;
	std::shared_ptr<UniformBufferInterface> createUniformBuffer(const void* data, size_t size) override;
	std::shared_ptr<StorageBufferInterface> createStorageBuffer(const void* data, size_t size, size_t stride) override;
	void setFrameBuffer(std::shared_ptr<FrameBufferInterface> frameBuffer) override;
	void resetFrameBuffer() override;
	void submit() override;
	void resetViewport() override;
	;
	void resetScissor() override;
	;
	void resetBlending() override;
	;
	void resetDepth() override;
	;
	void resetStencil() override;
	;

	void clear(bool clearColor, bool clearDepth, bool clearStencil) override;
	void setClearColor(const MATH::Vector4f& color) override;
	void setClearColor(float r, float g, float b, float a) override;

	// GPU-driven rendering contract (gpuFeatures.h); implemented in gpuDrivenVk.cpp.
	GpuFeatures mGpuFeatures;
	void configureGpuFeatures(std::vector<const char*>& extensions);
	GpuFeatures gpuFeatures() const override;
	std::shared_ptr<StorageBufferInterface> createStorageBuffer(const void* data, size_t count, size_t stride, BufferUsage usage) override;
	void fillBuffer(const std::shared_ptr<StorageBufferInterface>& buffer, size_t offset, size_t size, uint32_t value) override;
	void copyBuffer(const std::shared_ptr<StorageBufferInterface>& source, size_t sourceOffset,
		const std::shared_ptr<StorageBufferInterface>& destination, size_t destinationOffset, size_t size) override;
	std::vector<uint8_t> readBuffer(const std::shared_ptr<StorageBufferInterface>& buffer, size_t offset, size_t size) override;
	void drawIndexedIndirect(const std::shared_ptr<StorageBufferInterface>& arguments, size_t offset, uint32_t drawCount, uint32_t stride) override;
	void drawIndexedIndirectCount(const std::shared_ptr<StorageBufferInterface>& arguments, size_t offset,
		const std::shared_ptr<StorageBufferInterface>& count, size_t countOffset, uint32_t maxDrawCount, uint32_t stride) override;
	void dispatchIndirect(const std::shared_ptr<StorageBufferInterface>& arguments, size_t offset) override;
	void EnsureComputeState(vk::raii::CommandBuffer& cmdlist);
	void beginTransferCommands(vk::raii::CommandBuffer& cmdlist);
	void endTransferCommands(vk::raii::CommandBuffer& cmdlist);
	void flushRecordedCommands();

private:
	void init() override;
	bool checkValidationLayerSupport(const std::vector<const char*>& validationLayers);
	bool checkInstanceExtensionSupport(const std::vector<const char*>& extensionsToCheck);

	void wait();
	void createSwapchain(unsigned int windowID, uint32_t width, uint32_t height);
	void createSwapchainFrames(unsigned windowID, const std::vector<vk::Image>&);
	void nextFrame();
	uint32_t getBackbufferWidth();
	uint32_t getBackbufferHeight();
	vk::Format getBackbufferFormat();

	void beginRenderPass();
	void endRenderPass();

public:
	void cleanup() override { };

private:
	// std::shared_ptr<ShaderVk> mShader;
	std::shared_ptr<VertexBufferVk> mVertexBuffer;
	std::shared_ptr<IndexBufferVk> mIndexBuffer;

	PrimitiveMode mPrimitiveMode = PrimitiveMode::TRIANGLES;
	std::optional<Scissor> mScissor;
	std::optional<Viewport> mViewport;

	std::optional<Blending> mBlendMode;
	std::optional<Depth> mDepthMode = Depth();
	std::optional<Stencil> mStencilMode;
	// Same default as the OpenGL backend (no culling): material BackfaceCulling flags are not applied by the renderer yet
	CullFace mCullFace = CullFace::NONE;
	TriangleOrientation mTriangleOrientation = TriangleOrientation::CW;

	MATH::Vector4f mClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	float mDepthValue = 1.0f;
	float mStencilValue = 1.0f;

#if defined(DEBUG) || defined(_DEBUG)
	vk::raii::DebugUtilsMessengerEXT mDebugMessenger = nullptr;
#endif
};
}

#endif
