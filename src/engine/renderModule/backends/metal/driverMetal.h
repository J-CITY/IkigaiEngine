#pragma once
#ifdef METAL_BACKEND

#include <array>
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "../interface/driverInterface.h"
#include "bufferMetal.h"
#include "deviceMetal.h"
#include "profilerModule/gpuMemoryProfiler.h"
#include "shaderMetal.h"
#include "textureMetal.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL3/SDL_metal.h>
#include <dispatch/dispatch.h>

namespace IKIGAI::RENDER {

class DriverMetal : public DriverInterface {
public:
	DriverMetal();
	~DriverMetal() override;

	void init() override;
	void begin() override;
	void end() override;
	void waitForCompletion(GpuCompletionToken token) override;
	bool hasOpenRecording() const noexcept override {
		return mCurrentCommandBuffer != nil;
	}
	void submit() override;
	void cleanup() override;
	void resize(size_t width, size_t height) override;

	void setPrimitiveMode(PrimitiveMode topology) override;
	void setRasterization(RasterizationMode mode) override;
	void setViewport(const Viewport& viewport) override;
	void resetViewport() override;
	void setScissor(const Scissor& scissor) override;
	void resetScissor() override;

	void setShader(std::shared_ptr<ShaderInterface> shader) override;
	void setVertexBuffer(std::shared_ptr<VertexBufferInterface> buffer) override;
	void setIndexBuffer(std::shared_ptr<IndexBufferInterface> buffer) override;

	void setBlending(const Blending& value) override;
	void resetBlending() override;
	void setDepth(const Depth& depth) override;
	void resetDepth() override;
	void setStencil(const Stencil& stencil) override;
	void resetStencil() override;
	void setCull(CullFace cull_mode) override;
	void setTriangleOrientation(TriangleOrientation value) override;

	void clear(bool clearColor, bool clearDepth, bool clearStencil) override;
	void setClearColor(const MATH::Vector4f& color) override;
	void setClearColor(float r, float g, float b, float a) override;

	void draw(uint32_t count, uint32_t offset, uint32_t instance) override;
	void dispatch(uint32_t x, uint32_t y, uint32_t z) override;
	GpuFeatures gpuFeatures() const override;
	void fillBuffer(const std::shared_ptr<StorageBufferInterface>&, size_t offset, size_t size, uint32_t value) override;
	void fillBuffers(std::span<const BufferFill>) override;
	void copyBuffer(const std::shared_ptr<StorageBufferInterface>& source, size_t sourceOffset,
		const std::shared_ptr<StorageBufferInterface>& destination, size_t destinationOffset, size_t size) override;
	std::vector<uint8_t> readBuffer(const std::shared_ptr<StorageBufferInterface>&, size_t offset, size_t size) override;
	uint64_t requestBufferReadback(const std::shared_ptr<StorageBufferInterface>&, size_t offset, size_t size) override;
	bool collectBufferReadback(uint64_t ticket, std::vector<uint8_t>& bytes) override;
	void drawIndexedIndirect(const std::shared_ptr<StorageBufferInterface>&, size_t offset, uint32_t drawCount, uint32_t stride) override;
	void drawIndexedIndirectCount(const std::shared_ptr<StorageBufferInterface>& arguments, size_t offset,
		const std::shared_ptr<StorageBufferInterface>& count, size_t countOffset, uint32_t maxDrawCount, uint32_t stride) override;
	void dispatchIndirect(const std::shared_ptr<StorageBufferInterface>&, size_t offset) override;
	std::vector<float> readTexture(std::shared_ptr<TextureInterface> texture, unsigned mip, unsigned layer) override;
	std::shared_ptr<RayTracingProgram> createComputeProgram(const RayTracingProgramResource&) override;
	void dispatchComputeProgram(const RayTracingDispatch&) override;
#if USE_RAY_TRACING
	RayTracingCapabilities rayTracingCapabilities() const override;
	std::shared_ptr<RayTracingProgram> createRayTracingProgram(const RayTracingProgramResource&) override;
	void dispatchRayQueries(const RayTracingDispatch&) override;
	std::shared_ptr<VertexBufferInterface> createRayTracingVertexBuffer(const void*, size_t, size_t) override;
	std::shared_ptr<AccelerationStructure> createBottomLevelAS(const std::vector<RayTracingGeometry>&, bool = false) override;
	std::shared_ptr<AccelerationStructure> createTopLevelAS(const std::vector<RayTracingInstance>&, bool = false) override;
	void refitBottomLevelAS(const std::shared_ptr<AccelerationStructure>&) override;
	void refitTopLevelAS(const std::shared_ptr<AccelerationStructure>&, const std::vector<RayTracingInstance>&) override;
#endif
	BindlessCapabilities bindlessCapabilities() const override;
	uint32_t maxFragmentSampledTextures() const override {
		return 31;
	}
	bool supportsVertexPulling() const override {
		return true;
	}
	void draw(const DrawArguments& args) override;
	void drawIndexed(uint32_t count, uint32_t offset, uint32_t instance) override;
	void draw(const MeshInterface& mesh, PrimitiveMode primitive, uint32_t instances) override;

	void setPushConstant(ShaderType stage, uint32_t offset, uint32_t size, const void* data) override;

	void setTexture(size_t bind, std::shared_ptr<TextureInterface> data) override;
	void setUniformBuffer(size_t bind, std::shared_ptr<UniformBufferInterface> data) override;
	void setStorageBuffer(size_t bind, std::shared_ptr<StorageBufferInterface> data) override;
	void setTexture(const std::string& name, std::shared_ptr<TextureInterface> data) override;
	void setUniformBuffer(const std::string& name, std::shared_ptr<UniformBufferInterface> data) override;
	void setStorageBuffer(const std::string& name, std::shared_ptr<StorageBufferInterface> data) override;

	void setMSAA(bool value) override;

	std::shared_ptr<UniformBufferInterface> createUniformBuffer(const void* data, size_t size) override;
	std::shared_ptr<StorageBufferInterface> createStorageBuffer(const void* data, size_t size, size_t stride) override;
	std::shared_ptr<StorageBufferInterface> createStorageBuffer(const void* data, size_t size, size_t stride, BufferUsage usage) override;
	std::shared_ptr<TextureInterface> createTexture(
		const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) override;
	std::shared_ptr<TextureInterface> createTextureAtlas(
		const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) override;
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
	void setFrameBuffer(std::shared_ptr<FrameBufferInterface> frameBuffer) override;
	void resetFrameBuffer() override;

	id<MTLDevice> getDevice() const {
		return mDevice;
	}
	id<MTLCommandQueue> getCommandQueue() const {
		return mCommandQueue;
	}
	id<MTLCommandBuffer> getCurrentCommandBuffer() const {
		return mCurrentCommandBuffer;
	}
	id<MTLRenderCommandEncoder> getCurrentEncoder() const {
		return mCurrentEncoder;
	}
	id<MTLTexture> getSwapchainTexture() const {
		return mCurrentDrawable ? mCurrentDrawable.texture : nil;
	}
	id<MTLTexture> getDepthTexture() const {
		return mDepthTarget ? mDepthTarget->getTexture() : nil;
	}

	struct State {
		std::shared_ptr<ShaderMetal> mShader;
		std::shared_ptr<FrameBufferInterface> mFrameBuffer;
	};

	State mCurrentState;

private:
	friend class StorageBufferMetal;
	id<MTLCommandBuffer> beginGpuCommands(const char* operation);
	static void checkComputeGrid(MTLSize groups);
	void checkComputeThreads(id<MTLComputePipelineState> state, MTLSize threads) const;
	void encodeDispatch(MTLSize groups, id<MTLBuffer> arguments = nil, size_t offset = 0);
	void uploadStorageBuffer(id<MTLBuffer> destination, const void* data, size_t size, size_t offset);
	void encodeBufferCopy(id<MTLCommandBuffer> command, id<MTLBuffer> source, size_t sourceOffset,
		id<MTLBuffer> destination, size_t destinationOffset, size_t size);
	id<MTLComputePipelineState> bufferTransferPipeline(bool fill);
	MTLPrimitiveType currentPrimitiveType() const;

	struct BufferReadback {
		id<MTLBuffer> Staging = nil;
		size_t Offset = 0;
		size_t Size = 0;
	};
	struct PendingBufferReadback {
		BufferReadback Copy;
		std::shared_ptr<std::atomic<MTLCommandBufferStatus>> Status;
		uint64_t Ticket = 0;
	};
	BufferReadback encodeBufferReadback(id<MTLCommandBuffer> command, id<MTLBuffer> source, size_t offset, size_t size);

	void executeMetalProgram(const RayTracingDispatch&, bool rayQuery);
	void beginPass();
	void ensurePass();
	void endPass();
	void ensureTargets(NSUInteger width, NSUInteger height, NSUInteger samples);
	bool ensurePipeline(bool indirect = false);
	void ensureCountDrawResources(uint32_t capacity);
	void applyFixedState();
	void bindResources();
	void bindArgumentResources(ShaderType stage, id<MTLComputeCommandEncoder> compute = nil);
	std::vector<id<MTLBuffer>> mBindlessArgumentBuffers;
	NSUInteger mArgumentPageOffset = 0;
	void bindBufferSizeConstants(id<MTLComputeCommandEncoder> compute = nil);
	id<MTLBuffer> snapshotUniform(id<MTLBuffer> source, NSUInteger& outOffset);
	void bindMissingShaderResources();
	void ensureFallbackTexture();
	id<MTLBuffer> fallbackBuffer(NSUInteger bytes);
	void bindComputeResources(id<MTLComputeCommandEncoder> encoder, ShaderType stage = ShaderType::COMPUTE);
	void encodePreDraw(uint32_t count, bool indexed);
	id<MTLBuffer> ensureBuffer(id<MTLBuffer> slot, NSUInteger length);
	id<MTLComputePipelineState> computePipeline(id<MTLFunction> function);
	std::string pipelineKey() const;
	void updateDrawableSize(size_t width, size_t height);

	id<MTLDevice> mDevice = nil;
#if IKIGAI_ENABLE_MEMORY_TRACKING
	std::shared_ptr<PROFILER::GpuMemorySampler> mMemorySampler;
#endif
	id<MTLCommandQueue> mCommandQueue = nil;
	id<MTLCommandBuffer> mCurrentCommandBuffer = nil;
	id<MTLRenderCommandEncoder> mCurrentEncoder = nil;
	id<CAMetalDrawable> mCurrentDrawable = nil;
	SDL_MetalView mMetalView = nullptr;
	CAMetalLayer* mLayer = nil;
	dispatch_semaphore_t mInFlightSemaphore = nullptr;
	bool mHoldingFrame = false;

	std::shared_ptr<TextureMetal> mDepthTarget;
	id<MTLTexture> mMsaaColor = nil;
	NSUInteger mTargetWidth = 0;
	NSUInteger mTargetHeight = 0;
	NSUInteger mTargetSamples = 1;

	std::vector<MTLPixelFormat> mColorFormats;
	MTLPixelFormat mDepthFormat = MTLPixelFormatInvalid;
	bool mDepthHasStencil = false;
	NSUInteger mPassWidth = 1;
	NSUInteger mPassHeight = 1;
	NSUInteger mSampleCount = 1;

	MTLLoadAction mLoadColor = MTLLoadActionLoad;
	MTLLoadAction mLoadDepth = MTLLoadActionLoad;
	MTLLoadAction mLoadStencil = MTLLoadActionLoad;

	std::map<std::string, id<MTLRenderPipelineState>> mPipelines;
	std::map<std::string, id<MTLComputePipelineState>> mComputePipelines;
	id<MTLLibrary> mBufferTransferLibrary = nil;
	id<MTLArgumentEncoder> mCountDrawArgumentEncoder = nil;
	id<MTLComputePipelineState> mCountDrawPipeline = nil;
	id<MTLIndirectCommandBuffer> mCountDrawCommands = nil;
	id<MTLBuffer> mCountDrawBindings = nil;
	id<MTLBuffer> mCountDrawRange = nil;
	id<MTLComputePipelineState> mBufferFillPipeline = nil;
	id<MTLComputePipelineState> mBufferCopyPipeline = nil;
	std::deque<PendingBufferReadback> mPendingBufferReadbacks;
	uint64_t mNextBufferReadbackTicket = 1;
	std::map<std::string, id<MTLDepthStencilState>> mDepthStates;
	id<MTLBuffer> mUniformStaging = nil;
	NSUInteger mUniformStagingOffset = 0;
	id<MTLBuffer> mFallbackBuffer = nil;
	id<MTLTexture> mFallbackTexture = nil;
	id<MTLSamplerState> mFallbackSampler = nil;
	id<MTLBuffer> mStageVertexOutput = nil;
	id<MTLBuffer> mStagePatchOutput = nil;
	id<MTLBuffer> mStageTessFactors = nil;

	std::shared_ptr<VertexBufferMetal> mVertexBuffer;
	std::shared_ptr<IndexBufferMetal> mIndexBuffer;
	std::map<size_t, std::shared_ptr<TextureMetal>> mTextures;
	std::map<size_t, std::shared_ptr<UniformBufferMetal>> mUniformBuffers;
	std::map<size_t, std::shared_ptr<StorageBufferMetal>> mStorageBuffers;

	PrimitiveMode mPrimitiveMode = PrimitiveMode::TRIANGLES;
	RasterizationMode mRasterizationMode = RasterizationMode::FILL;
	std::optional<Viewport> mViewport;
	std::optional<Scissor> mScissor;
	std::optional<Blending> mBlendMode;
	std::optional<Depth> mDepthMode = Depth();
	std::optional<Stencil> mStencilMode;
	CullFace mCullFace = CullFace::NONE;
	TriangleOrientation mTriangleOrientation = TriangleOrientation::CW;
	bool mMSAA = false;

	MATH::Vector4f mClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	std::array<uint8_t, 256> mPushConstants {};
	uint32_t mPushBytes = 0;

	size_t mWidth = 0;
	size_t mHeight = 0;
};

std::unique_ptr<DriverInterface> CreateDriverMetal();

}

#endif
