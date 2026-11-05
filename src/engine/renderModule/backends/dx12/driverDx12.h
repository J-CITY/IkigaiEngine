#pragma once
#ifdef DX12_BACKEND
#include <bitset>
#include <deque>
#include <dxgi1_6.h>
#include <map>

#include "../interface/driverInterface.h"
#include "d3dx12/d3dx12.h"
#include "frameBufferDx12.h"
#include "renderModule/backends/interface/renderEnums.h"
#include "renderModule/backends/interface/textureInterface.h"
#include "shaderDx12.h"
#include <array>
#include <d3d12.h>
#include <functional>
#include <memory>
#include <vector>
#include <wrl/client.h>

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "d3dUtil.h"

namespace IKIGAI::RENDER {
struct StreamlineCallbacks;
class StorageBufferDx12;
class StorageBufferInterface;
class MeshInterface;
class IndexBufferInterface;
class VertexBufferInterface;
class UniformBufferDx12;
class FrameBufferDx12;
class IndexBufferDx12;
class VertexBufferDx12;
class ShaderDx12;
class UploadRingDx12;
class ImmediateSubmitDx12;

class DriverDx12 : public IKIGAI::RENDER::DriverInterface {
	friend class FsrFrameGenerationDx12;
	friend class StreamlineFrameGenerationDx12;
	struct State {
		std::shared_ptr<ShaderDx12> mShader;
		CullFace mCullFace = CullFace::NONE;
		TriangleOrientation mTriangleOrientation = TriangleOrientation::CW;
		std::optional<Depth> mDepth;
		std::optional<Blending> mBlend;
		std::optional<Stencil> mStencil;
		RasterizationMode mRasterization = RasterizationMode::FILL;
		std::shared_ptr<FrameBufferDx12> mFrameBuffer;

		[[nodiscard]] std::string getId() const;
	};

public:
	void waitForCompletion(GpuCompletionToken token) override;
	bool hasOpenRecording() const noexcept override {
		return mRecording;
	}
	static constexpr int DEFAULT_FB_SIZE = 2;
	static constexpr DXGI_FORMAT DefaultTextureColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	static constexpr DXGI_FORMAT DefaultDepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	static std::vector<CD3DX12_STATIC_SAMPLER_DESC> GetStaticSamplers();

	DriverDx12();
	~DriverDx12() override;
	RayTracingCapabilities mRayTracingCapabilities;
	std::shared_ptr<void> mDlssRuntime;
	std::unique_ptr<UpscalerInterface> createUpscaler(UpscalerType) override;
	std::unique_ptr<FrameGenerationInterface> createFrameGeneration(FrameGenerationType) override;
	void renderPresentationUI(std::shared_ptr<FrameBufferInterface>) override;
	void initializeStreamline();
	void bindStreamlineDevice();
	StreamlineCallbacks streamlineCallbacks();
	void streamlinePresenter(bool enable);
	void setTemporalCamera(const FrameGenerationInputs&) override;
	void latencyFrameBegin() override;
	void latencySimulationEnd() override;
	void latencySubmitBegin() override;
	void latencySubmitEnd() override;
	void invalidateUpscalerState();
	Microsoft::WRL::ComPtr<ID3D12Device5> mRayDevice;
	Microsoft::WRL::ComPtr<ID3D12Device> mStreamlineDeviceProxy;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> mStreamlineQueueProxy;
	bool mShuttingDown = false;
	DWORD mDebugCallbackCookie = 0;
	std::vector<std::shared_ptr<void>> mRayFrameResources, mRayDeferred;
	std::vector<std::shared_ptr<TextureDx12>> mImGuiTextures;
	std::map<std::string, std::weak_ptr<RayTracingProgram>> mRayPrograms;
	RayTracingCapabilities rayTracingCapabilities() const override {
		return mRayTracingCapabilities;
	}
	void configureRayTracing();
	std::shared_ptr<VertexBufferInterface> createRayTracingVertexBuffer(const void*, size_t, size_t) override;
	std::shared_ptr<RayTracingProgram> createRayTracingProgram(const RayTracingProgramResource&) override;
	std::shared_ptr<RayTracingProgram> createComputeProgram(const RayTracingProgramResource&) override;
	std::shared_ptr<RayTracingProgram> createRayProgram(const RayTracingProgramResource&, bool query);
	std::shared_ptr<AccelerationStructure> createBottomLevelAS(const std::vector<RayTracingGeometry>&, bool allowUpdate = false) override;
	std::shared_ptr<AccelerationStructure> createTopLevelAS(const std::vector<RayTracingInstance>&, bool allowUpdate = false) override;
	void refitBottomLevelAS(const std::shared_ptr<AccelerationStructure>&) override;
	void refitTopLevelAS(const std::shared_ptr<AccelerationStructure>&, const std::vector<RayTracingInstance>&) override;
	void traceRays(const RayTracingDispatch&) override;
	void dispatchRayQueries(const RayTracingDispatch&) override;
	void dispatchComputeProgram(const RayTracingDispatch&) override;
	void executeRayProgram(const RayTracingDispatch&, bool compute);

	void onResize();
	void setPrimitiveMode(PrimitiveMode param) override;
	void setRasterization(RasterizationMode param) override;
	void setViewport(const Viewport& param) override;
	void resetViewport() override;
	void setScissor(const Scissor& param) override;
	void resetScissor() override;
	void setBlending(const Blending& param) override;
	void resetBlending() override;
	void setDepth(const Depth& param) override;
	void resetDepth() override;
	void setStencil(const Stencil& param) override;
	void resetStencil() override;
	void setCull(CullFace param) override;
	void setMSAA(bool value) override;
	bool get4xMsaaState() const;
	void setClearColor(const MATH::Vector4f& color) override;
	void setClearColor(float r, float g, float b, float a) override;
	void setVertexBuffer(std::shared_ptr<VertexBufferInterface> buffer) override;
	void setIndexBuffer(std::shared_ptr<IndexBufferInterface> buffer) override;
	void setShader(std::shared_ptr<ShaderInterface> shader) override;
	void setPushConstant(ShaderType stage, uint32_t offset, uint32_t size, const void* data) override;
	void setTriangleOrientation(TriangleOrientation value) override {
		mCurrentState.mTriangleOrientation = value;
	};

	void draw(uint32_t vertex_count, uint32_t vertex_offset, uint32_t instance_count) override;
	std::vector<float> readTexture(std::shared_ptr<TextureInterface> texture, unsigned mip, unsigned layer) override;
	void dispatch(uint32_t x, uint32_t y, uint32_t z) override;
	void renderGraphBarrier(bool uav) override;
	// Texture infrastructure for GI/HZB (M1-4, M1-8); implemented in textureFeaturesDx12.cpp.
	void setStorageImage(size_t binding, std::shared_ptr<TextureInterface> texture, uint32_t mip, uint32_t layer) override;
	// Sets StorageImageMipViews, HalfFloatTextures and SamplerMinMaxReduction.
	void fillTextureGpuFeatures(GpuFeatures& features) const;
	BindlessCapabilities bindlessCapabilities() const override;
	uint32_t maxFragmentSampledTextures() const override {
		return 128;
	}
	bool supportsVertexPulling() const override {
		return true;
	}
	void draw(const DrawArguments& args) override;
	void drawIndexed(uint32_t index_count, uint32_t index_offset, uint32_t instance_count) override;
	// Timestamp queries for GpuStageProfiler (timestampQueriesDx12.cpp).
	uint32_t timestampQueryCapacity() const override;
	void writeTimestamp(uint32_t index) override;
	bool readTimestamps(uint32_t first, uint32_t count, std::vector<uint64_t>& ticks) override;
	void draw(const MeshInterface& mesh, PrimitiveMode primitive, uint32_t instances) override;

	void resize(size_t width, size_t height) override;
	void submit() override;
	Microsoft::WRL::ComPtr<ID3D12Device> getDevice();
	D3D12MA::Allocator* getAllocator() const {
		return mAllocator.Get();
	}
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> getCommandList();
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> getCommandQueue() {
		return mCommandQueue;
	}
	void destroyDeferred(Microsoft::WRL::ComPtr<ID3D12DeviceChild> object);
	void destroyDeferred(Dx12Resource object);
	std::vector<Microsoft::WRL::ComPtr<ID3D12DeviceChild>>& getDestroyDeferredObjects();
	void clear(bool clearColor, bool clearDepth, bool clearStencil) override;
	std::shared_ptr<FrameBufferDx12>& getDefaultFrameBuffer() {
		return mSwapchains[mCurrentWindowID].mDefaultFb[mFrameId];
	}
	uint32_t getFrameBufferWidth() const {
		return mCurrentState.mFrameBuffer ? mCurrentState.mFrameBuffer->getTextures()[0]->getWidth() : mSwapchains.at(mCurrentWindowID).width;
	}

	uint32_t getFrameBufferHeight() const {
		return mCurrentState.mFrameBuffer ? mCurrentState.mFrameBuffer->getTextures()[0]->getHeight() : mSwapchains.at(mCurrentWindowID).height;
	}

	PixelFormat getFrameBufferFormat() const;

	void beginCommandList(ID3D12CommandAllocator* cmd_alloc, ID3D12GraphicsCommandList* cmd_list);
	void endCommandList(ID3D12CommandQueue* cmd_queue, ID3D12GraphicsCommandList* cmd_list, bool wait_for);

	void setTexture(size_t bind, std::shared_ptr<TextureInterface> data) override;
	void setUniformBuffer(size_t bind, std::shared_ptr<UniformBufferInterface> data) override;
	void setStorageBuffer(size_t bind, std::shared_ptr<StorageBufferInterface> data) override;

	void setTexture(const std::string& bind, std::shared_ptr<TextureInterface> data) override;
	void setUniformBuffer(const std::string& bind, std::shared_ptr<UniformBufferInterface> data) override;
	void setStorageBuffer(const std::string& bind, std::shared_ptr<StorageBufferInterface> data) override;

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

	std::shared_ptr<UniformBufferInterface> createUniformBuffer(const void* data, size_t size) override;
	std::shared_ptr<StorageBufferInterface> createStorageBuffer(const void* data, size_t size, size_t stride) override;
	std::shared_ptr<FrameBufferInterface> createFrameBuffer(const std::vector<std::shared_ptr<TextureInterface>>& textures,
		std::shared_ptr<TextureInterface> depth, unsigned mip = 0, unsigned layer = 0) override;

	void setFrameBuffer(std::shared_ptr<FrameBufferInterface> frameBuffer) override;
	void resetFrameBuffer() override;

	CD3DX12_CPU_DESCRIPTOR_HANDLE& getDescriptorHeapCPUHandle() {
		if (mDescriptorHeap
			&& mDescriptorHeapCPUHandle.ptr >= mDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(1000) * mDescriptorIncSize) {
			throw std::runtime_error("Legacy descriptor range exhausted; bindless/ImGui ranges are reserved");
		}
		return mDescriptorHeapCPUHandle;
	}
	// Slots of the legacy range [0, 1000) of the shader-visible heap used by textures and storage buffers.
	// Released slots are recycled only after the end-of-frame GPU wait, so recorded commands never see them reused.
	static constexpr uint32_t InvalidLegacyDescriptor = UINT32_MAX;
	uint32_t allocateLegacyDescriptors(uint32_t count);
	void releaseLegacyDescriptors(uint32_t index, uint32_t count);
	D3D12_CPU_DESCRIPTOR_HANDLE legacyDescriptorCpu(uint32_t index) const;
	D3D12_GPU_DESCRIPTOR_HANDLE legacyDescriptorGpu(uint32_t index) const;
	CD3DX12_GPU_DESCRIPTOR_HANDLE& getDescriptorHeapGPUHandle() {
		return mDescriptorHeapGPUHandle;
	}
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& getDescriptorHeap() {
		return mDescriptorHeap;
	}
	unsigned getDescriptorIncSize() {
		return mDescriptorIncSize;
	}
	// GPU-driven rendering contract (gpuFeatures.h); implemented in gpuDrivenDx12.cpp.
	GpuFeatures gpuFeatures() const override;
	std::shared_ptr<StorageBufferInterface> createStorageBuffer(const void* data, size_t count, size_t stride, BufferUsage usage) override;
	void fillBuffer(const std::shared_ptr<StorageBufferInterface>& buffer, size_t offset, size_t size, uint32_t value) override;
	void copyBuffer(const std::shared_ptr<StorageBufferInterface>& source, size_t sourceOffset,
		const std::shared_ptr<StorageBufferInterface>& destination, size_t destinationOffset, size_t size) override;
	std::vector<uint8_t> readBuffer(const std::shared_ptr<StorageBufferInterface>& buffer, size_t offset, size_t size) override;
	uint64_t requestBufferReadback(const std::shared_ptr<StorageBufferInterface>& buffer, size_t offset, size_t size) override;
	bool collectBufferReadback(uint64_t ticket, std::vector<uint8_t>& bytes) override;
	// Asynchronous readbacks: each request copies into its own READBACK-heap buffer; it is complete once mFence
	// reached the value that wait() signals after the submission containing the copy.
	struct PendingReadbackDx12 {
		uint64_t Ticket = 0;
		UINT64 FenceValue = 0;
		size_t Size = 0;
		// Bytes of Readback (>= Size): collected buffers are recycled for later requests.
		size_t Capacity = 0;
		Dx12Resource Readback {};
	};
	std::deque<PendingReadbackDx12> mPendingReadbacks {};
	std::vector<PendingReadbackDx12> mFreeReadbacks {};
	uint64_t mNextReadbackTicket = 1;
	void drawIndexedIndirect(const std::shared_ptr<StorageBufferInterface>& arguments, size_t offset, uint32_t drawCount, uint32_t stride) override;
	void drawIndexedIndirectCount(const std::shared_ptr<StorageBufferInterface>& arguments, size_t offset,
		const std::shared_ptr<StorageBufferInterface>& count, size_t countOffset, uint32_t maxDrawCount, uint32_t stride) override;
	void dispatchIndirect(const std::shared_ptr<StorageBufferInterface>& arguments, size_t offset) override;
	// Executes everything recorded so far on the queue without waiting and continues recording into the same list.
	// ImGui platform windows execute their own command lists immediately; this orders them after the frame's barriers.
	void executeRecordedCommands();
	void allocImGuiSrv(D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu);
	void freeImGuiSrv(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu);
	// Transient upload memory for constant buffer snapshots and staging copies recorded in the current frame.
	UploadRingDx12& getUploadRing();
	// Records, submits and waits for a one-off command list on the reusable immediate queue.
	void immediateSubmit(const std::function<void(ID3D12GraphicsCommandList*)>& record);

private:
	void begin() override;
	void end() override;

	MATH::Vector4f mClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	float mDepthValue = 1.0f;
	float mStencilValue = 1.0f;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mBindlessSamplerHeap;
	uint32_t mBindlessDescriptorCursor = 1064;
	uint32_t mBindlessSamplerCursor = 0;
	std::pair<D3D12_GPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> bindlessSnapshot(
		const ShaderReflection::Uniform&, uint32_t, const BindlessResourceTable* localTable = nullptr);
	std::map<std::string, std::pair<D3D12_GPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE>> mBindlessSnapshots;
	struct StorageImageBinding {
		std::shared_ptr<TextureDx12> Texture;
		uint32_t Mip = 0;
		uint32_t Layer = 0;
	};
	// Per-mip/per-layer storage image overrides; setTexture on the same binding removes the override.
	std::map<size_t, StorageImageBinding> mStorageImageBindings;
	// Per-frame UAV for one mip/layer, allocated from the transient bindless descriptor range.
	D3D12_GPU_DESCRIPTOR_HANDLE storageImageUav(const StorageImageBinding& binding);
	CD3DX12_CPU_DESCRIPTOR_HANDLE mDescriptorHeapCPUHandle {};
	CD3DX12_GPU_DESCRIPTOR_HANDLE mDescriptorHeapGPUHandle {};
	unsigned int mDescriptorIncSize = 0;
	std::vector<UINT> mImGuiFreeSrv;
	static constexpr uint32_t LegacyDescriptorCapacity = 1000;
	uint32_t mLegacyDescriptorCursor = 0;
	// Free slot runs by run length (index 1 and 2 are used), plus runs waiting for the next GPU wait.
	std::array<std::vector<uint32_t>, 3> mFreeLegacyDescriptors;
	std::vector<std::pair<uint32_t, uint32_t>> mPendingLegacyDescriptors;

	std::vector<Microsoft::WRL::ComPtr<ID3D12DeviceChild>> mDestroyDeffered {};
	std::vector<Dx12Resource> mDestroyDeferredResources {};
	// Upload pages are retired with mFenceValue in end() and recycled in begin() once the fence passed it.
	std::unique_ptr<UploadRingDx12> mUploadRing;
	std::unique_ptr<ImmediateSubmitDx12> mImmediateSubmit;

	struct SwapchainContextDx12 {
		Microsoft::WRL::ComPtr<IDXGISwapChain3> mSwapChain;
		std::array<std::shared_ptr<FrameBufferDx12>, DEFAULT_FB_SIZE> mDefaultFb;
		unsigned width = 0;
		unsigned height = 0;
		bool streamline = false;
		unsigned slMultiplier = 0;
	};

	std::unordered_map<unsigned int, SwapchainContextDx12> mSwapchains;
	unsigned int mCurrentWindowID = 0;

	std::map<size_t, std::shared_ptr<TextureDx12>> mTextures {};
	std::map<size_t, std::shared_ptr<UniformBufferDx12>> mUniformBuffers {};
	std::map<size_t, std::shared_ptr<StorageBufferDx12>> mStorageBuffers {};

	struct PendingPushConstant {
		uint32_t offsetDwords = 0;
		std::vector<uint32_t> values;
	};
	std::vector<PendingPushConstant> mPendingPushConstants;
	void flushPushConstants(bool compute);

	Microsoft::WRL::ComPtr<ID3D12Device> mDevice;
	Microsoft::WRL::ComPtr<D3D12MA::Allocator> mAllocator;
#if IKIGAI_ENABLE_MEMORY_TRACKING
	std::shared_ptr<PROFILER::GpuMemorySampler> mMemorySampler;
#endif

	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> mCommandList;

	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mCommandAllocator;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;

	Microsoft::WRL::ComPtr<IDXGIFactory6> mDxgiFactory;

	unsigned int mFrameId = 0;
	bool mRecording = false;
	HANDLE mFenceEvent = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
	UINT64 mFenceValue {};
	Microsoft::WRL::ComPtr<ID3D12QueryHeap> mTimestampHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource> mTimestampReadback;
	Microsoft::WRL::ComPtr<ID3D12Fence> mTimestampFence;
	UINT64 mTimestampSerial = 0;
	uint32_t mTimestampCapacity = 0;
	double mTimestampPeriodNanoseconds = 0.0;
	std::vector<uint32_t> mTimestampsWritten;
	// Per query: timestamp fence value after which its resolved value is readable; 0 = none.
	std::vector<UINT64> mTimestampResolveSerial;
	void createTimestampQueries();
	bool resolveTimestampQueries();
	void signalTimestampQueries();

	State mCurrentState;
	std::map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mStates {};

	PrimitiveMode mPrimitiveMode = PrimitiveMode::TRIANGLES;
	std::optional<Viewport> mViewport;
	std::optional<Scissor> mScissor;
	std::shared_ptr<VertexBufferDx12> mVertexBuffer;
	std::shared_ptr<IndexBufferDx12> mIndexBuffer;

	unsigned m4xMsaaQuality = 0;
	bool m4xMsaaState = true;
	unsigned mWidth = 0;
	unsigned mHeight = 0;

	void init() override;
	void createCommandList();
	void createDescriptorHeaps();
	void createSwapChain(unsigned int windowID, unsigned width, unsigned height, bool streamline = false);
	void logAdapters();
	void logOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format);
	Microsoft::WRL::ComPtr<ID3D12PipelineState> createNewState(const State& state);
	void applyState();
	void createDefaultFrameBuffer(unsigned int windowID, unsigned width, unsigned height);
	void logAdapterOutputs(IDXGIAdapter* adapter);
	void clearForDestroy();
	void wait();
	void presentFrame() override;
	void waitForPresentedFrame() override;
	void finishPresent() override;
	void releaseCompletedFrame();

	FrameInfo mFrameInfo;

	GpuFeatures mGpuFeatures;
	std::map<uint32_t, Microsoft::WRL::ComPtr<ID3D12CommandSignature>> mDrawIndexedSignatures;
	Microsoft::WRL::ComPtr<ID3D12CommandSignature> mDispatchSignature;
	void configureGpuFeatures();
	ID3D12CommandSignature* drawIndexedSignature(uint32_t stride);
	void flushRecordedCommands();

public:
	void cleanup() override { };
};

}

#endif
