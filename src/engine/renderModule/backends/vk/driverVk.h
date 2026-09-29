#pragma once
#include <bitset>
#include <memory>
#include <memory>
#include <unordered_set>

#include "indexBufferVk.h"
#include "shaderVk.h"
#include "vertexBufferVk.h"
#include "mathModule/math.h"
#include "renderModule/backends/interface/storageBufferInterface.h"
#include "renderModule/backends/interface/textureInterface.h"

#ifdef VULKAN_BACKEND
#include <queue>
#include "swapChainHandler.h"
#include "commandHandler.h"
#include <assimp/scene.h>
#include "../interface/driverInterface.h"

struct ImDrawData;

namespace IKIGAI::RENDER {
	class StorageBufferVk;
	class UniformBufferVk;
	class MeshInterface;
	class ShaderVk;
	class FrameBufferVk;
	class CommandBuffer;
	
	class DriverVk : public DriverInterface {
	public:
		vk::raii::Context mContext;
		vk::raii::Instance mInstance = nullptr;
		vk::raii::PhysicalDevice mPhysicalDevice = nullptr;
		vk::raii::Queue mQueue = nullptr;
		vk::raii::Device mDevice = nullptr;
		uint32_t mQueueFamilyIndex = -1;
		vk::SurfaceFormatKHR mSurfaceFormat;
		
		vk::raii::CommandPool mCommandPool = nullptr;

		constexpr static vk::Format DefaultDepthStencilFormat = vk::Format::eD32SfloatS8Uint;

		bool working = false;

		bool render_pass_active = false;
		struct Frame {
			vk::raii::Fence fence = nullptr;
			std::shared_ptr<FrameBufferVk> mFrameBuffer;
			vk::raii::Semaphore mImageAcquiredSemaphore = nullptr;
			vk::raii::Semaphore mRenderCompleteSemaphore = nullptr;
			vk::raii::CommandBuffer mCommandBuffer = nullptr;
		};
		
		struct SwapchainContextVk {
			vk::raii::SurfaceKHR surface = nullptr;
			vk::raii::SwapchainKHR swapchain = nullptr;
			std::vector<Frame> frames;
			uint32_t width = 0;
			uint32_t height = 0;
		};

		std::unordered_map<unsigned int, SwapchainContextVk> mSwapchains;
		unsigned int mCurrentWindowID = 0;

		uint32_t mSemaphoreIndex = 0;
		uint32_t mFrameIndex = 0;

		struct State {
			std::shared_ptr<ShaderVk> mShader;
			std::shared_ptr<FrameBufferVk> mFrameBuffer;

			// std::vector<vk::Format> color_attachment_formats;
			// std::optional<vk::Format> depth_stencil_format;
			//std::vector<InputLayout> input_layouts;

			std::string getName();
		};

		State mCurrentState;
		std::map<std::string, vk::raii::Pipeline> mStates;


		VkDescriptorPool mImguiPool;

		//Frame& getCurrentFrame() { return frames.at(frame_index); }

		//std::unordered_map<uint32_t, TextureVK*> textures;
		//std::unordered_map<uint32_t, UniformBufferVK*> uniform_buffers;
		//std::unordered_map<uint32_t, StorageBufferVK*> storage_buffers;
		//std::unordered_map<uint32_t, TopLevelAccelerationStructureVK*> top_level_acceleration_structures;

		//std::unordered_map<PipelineStateVK, vk::raii::Pipeline> pipeline_states;

		//RaytracingPipelineStateVK raytracing_pipeline_state;
		//std::unordered_map<RaytracingPipelineStateVK, vk::raii::Pipeline> raytracing_pipeline_states;

		//SamplerStateVK sampler_state;
		//std::unordered_map<SamplerStateVK, vk::raii::Sampler> sampler_states;

		//std::vector<RenderTargetVK*> render_targets;

		//PipelineStateVK pipeline_state;
		//std::optional<Scissor> scissor;
		//std::optional<Viewport> viewport;
		//std::optional<DepthMode> depth_mode = DepthMode();
		//std::optional<StencilMode> stencil_mode;
		//CullMode cull_mode = CullMode::None;
		//FrontFace front_face = FrontFace::Clockwise;
		//Topology topology = Topology::TriangleList;
		//std::vector<VertexBufferVK*> vertex_buffers; // TODO: store pointer and count, not std::vector
		//IndexBufferVK* index_buffer = nullptr;
		//std::optional<BlendMode> blend_mode;

		//bool pipeline_state_dirty = true;
		//bool scissor_dirty = true;
		//bool viewport_dirty = true;
		//bool depth_mode_dirty = true;
		//bool stencil_mode_dirty = true;
		//bool cull_mode_dirty = true;
		//bool front_face_dirty = true;
		//bool topology_dirty = true;
		//bool vertex_buffers_dirty = true;
		//bool index_buffer_dirty = true;
		//bool blend_mode_dirty = true;

		std::unordered_set<uint32_t> graphics_pipeline_ignore_bindings;
		//
		//uint32_t getBackbufferWidth();
		//uint32_t getBackbufferHeight();
		//vk::Format getBackbufferFormat();
		//
		//bool render_pass_active = false;
		//
		vk::PipelineStageFlags2 mCurrentMemoryStage = vk::PipelineStageFlagBits2::eTransfer;
		//
		//std::unordered_set<ObjectVK*> objects;

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

		SwapchainContextVk& getCurrentSwapchainContext() { return mSwapchains[mCurrentWindowID]; }
		Frame& getCurrentFrame() { return getCurrentSwapchainContext().frames.at(mFrameIndex); }

		void activateRenderPass();
		void deactivateRenderPass();

		using VulkanObject = std::variant<
			vk::raii::Buffer,
			vk::raii::Image,
			vk::raii::DeviceMemory,
			vk::raii::Pipeline,
			vk::raii::AccelerationStructureKHR
		>;
		std::vector<VulkanObject> mDestroyDeferred{};
		void destroyDeferred(VulkanObject&& object);
		std::vector<VulkanObject>& getDestroyDeferredObjects();
		void EnsureMemoryState(const vk::raii::CommandBuffer& cmdbuf, vk::PipelineStageFlags2 stage);
		void PushDescriptorBuffer(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
		                          const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding,
		                          vk::DescriptorType type,
		                          const vk::raii::Buffer& buffer);
		void PushDescriptorTexture(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
		                           const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding);
		void PushDescriptorUniformBuffer(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
		                                 const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding);
		void PushDescriptorStorageImage(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
		                                const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding);
		void PushDescriptorStorageBuffer(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
		                                 const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding);
		void PushDescriptors(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
		                     const vk::raii::PipelineLayout& pipeline_layout,
		                     const std::vector<vk::DescriptorSetLayoutBinding>& required_descriptor_bindings,
		                     const std::unordered_set<uint32_t>& ignore_bindings);
		void PushDescriptorAccelerationStructure(vk::raii::CommandBuffer& cmdlist,
		                                         vk::PipelineBindPoint pipeline_bind_point,
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

		std::shared_ptr<TextureInterface> createTexture(const std::string& path, bool generateMipmap = true, UTILS::IAllocator* allocator = nullptr, ResourceDeleter deleter = nullptr) override;
		std::shared_ptr<TextureInterface> createTextureAtlas(const std::string& path, bool generateMipmap = true, UTILS::IAllocator* allocator = nullptr, ResourceDeleter deleter = nullptr) override;
		std::shared_ptr<TextureInterface> createTexture(const TextureResource& res, UTILS::IAllocator* allocator = nullptr, ResourceDeleter deleter = nullptr) override;
		std::shared_ptr<TextureInterface> createTexture(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, ResourceDeleter deleter = nullptr) override;
		std::shared_ptr<TextureInterface> createTextureAtlas(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, ResourceDeleter deleter = nullptr) override;
		std::shared_ptr<TextureInterface> createTexture(const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, ResourceDeleter deleter = nullptr) override;

		std::shared_ptr<ShaderInterface> createShader(const std::string& vertexPath, const std::string& fragmentPath) override;
		std::shared_ptr<ShaderInterface> createShader(const ShaderResource& res, UTILS::IAllocator* allocator = nullptr, ShaderDeleter deleter = nullptr) override;

		std::shared_ptr<ModelInterface> createModel(const std::string& path, UTILS::IAllocator* allocator = nullptr, ModelDeleter deleter = nullptr) override;

		std::shared_ptr<MaterialInterface> createMaterial(const MaterialResource& res, UTILS::IAllocator* allocator = nullptr, MaterialDeleter deleter = nullptr) override;

		std::shared_ptr<FrameBufferInterface> createFrameBuffer(const std::vector<std::shared_ptr<TextureInterface>>& textures, std::shared_ptr<TextureInterface> depth) override;

		void setBlending(const Blending& value) override;
		void setDepth(const Depth& depth) override;
		void setStencil(const Stencil& stencil) override;
		void setCull(CullFace cull_mode) override;
		void setTriangleOrientation(TriangleOrientation value) override;
		void draw(uint32_t vertex_count, uint32_t vertex_offset, uint32_t instance_count) override;
		void drawIndexed(uint32_t index_count, uint32_t index_offset, uint32_t instance_count) override;
		void submit() override;
		void resetViewport() override;;
		void resetScissor() override;;
		void resetBlending() override;;
		void resetDepth() override;;
		void resetStencil() override;;


		void clear(bool clearColor, bool clearDepth, bool clearStencil) override;
		void setClearColor(const MATH::Vector4f& color) override;
		void setClearColor(float r, float g, float b, float a) override;
	private:
		void init() override;
		bool checkValidationLayerSupport(const std::vector<const char*>& validationLayers);
		bool checkInstanceExtensionSupport(const std::vector<const char*>& extensionsToCheck);

		void wait();
		void createSwapchain(unsigned int windowID, uint32_t width, uint32_t height);
		void nextFrame();
		uint32_t getBackbufferWidth();
		uint32_t getBackbufferHeight();
		vk::Format getBackbufferFormat();

		void beginRenderPass();
		void endRenderPass();

	public:
		void cleanup() override {};

	private:
		//std::shared_ptr<ShaderVk> mShader;
		std::shared_ptr<VertexBufferVk> mVertexBuffer;
		std::shared_ptr<IndexBufferVk> mIndexBuffer;

		PrimitiveMode mPrimitiveMode = PrimitiveMode::TRIANGLES;
		std::optional<Scissor> mScissor;
		std::optional<Viewport> mViewport;

		std::optional<Blending> mBlendMode;
		std::optional<Depth> mDepthMode = Depth();
		std::optional<Stencil> mStencilMode;
		CullFace mCullFace = CullFace::BACK;
		TriangleOrientation mTriangleOrientation = TriangleOrientation::CW;

		MATH::Vector4f mClearColor = {0.0f, 0.0f, 0.0f, 1.0f};
		float mDepthValue = 1.0f;
		float mStencilValue = 1.0f;

#if defined(DEBUG) || defined(_DEBUG) 
		vk::raii::DebugUtilsMessengerEXT mDebugMessenger = nullptr;
#endif
		
	};
}

#endif
