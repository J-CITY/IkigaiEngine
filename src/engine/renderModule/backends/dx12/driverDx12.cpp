#include "utilsModule/memory/resourceAllocator.h"
#include "driverDx12.h"
#include "renderModule/backends/driverFactory.h"
#include "vertexFormatsDx12.h"
#include "captureModule/screenshot.h"
#ifdef IKIGAI_USE_STREAMLINE
#include "../streamlineRuntime.h"
#endif

#include "renderModule/backends/interface/meshInterface.h"
#include <algorithm>
#include <cstring>

#ifdef DX12_BACKEND
#include "frameBufferDx12.h"
#include "windowMessageWait.h"
#include "indexBufferDx12.h"
#include "storageBufferDx12.h"
#include "shaderDx12.h"
#include "textureDx12.h"
#include "vertexBufferDx12.h"
#include "uniformBufferDx12.h"
#include "uploadRingDx12.h"
#include "immediateSubmitDx12.h"
#include "backends/imgui_impl_dx12.h"
#include "utilsModule/imguiHelper/imguiBackend/imguiBackend.h"
#include "d3dx12/DirectXHelpers.h"
#include "renderModule/backends/interface/pixelFormatConversion.h"
#include "renderModule/backends/interface/renderEnums.h"
#include "renderModule/backends/interface/resourceStruct.h"
#include "resourceModule/serviceManager.h"
#include "resourceModule/modelManager.h"
#include "resourceModule/materialManager.h"
#include "utilsModule/log/loggerDefine.h"
#include "modelDx12.h"
#include "materialDx12.h"
#include "windowModule/window/window.h"
#include <dxgi1_6.h>
#pragma comment(lib, "dxgi")

using namespace IKIGAI;
using namespace IKIGAI::RENDER;

const static std::map<PrimitiveMode, D3D_PRIMITIVE_TOPOLOGY> TopologyMap = {
	{PrimitiveMode::POINTS, D3D_PRIMITIVE_TOPOLOGY_POINTLIST},
	{PrimitiveMode::LINES, D3D_PRIMITIVE_TOPOLOGY_LINELIST},
	{PrimitiveMode::LINE_STRIP, D3D_PRIMITIVE_TOPOLOGY_LINESTRIP},
	{PrimitiveMode::TRIANGLES, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST},
	{PrimitiveMode::TRIANGLE_STRIP, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP},
	{PrimitiveMode::LINES_ADJACENCY, D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ},
	{PrimitiveMode::LINE_STRIP_ADJACENCY, D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ},
	{PrimitiveMode::TRIANGLES_ADJACENCY, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ},
	{PrimitiveMode::TRIANGLE_STRIP_ADJACENCY, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ},
	{PrimitiveMode::PATCHES, D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST},
	//Not support
	//{PrimitiveMode::LINE_LOOP, }
	//{PrimitiveMode::TRIANGLE_FAN, }
};

const static std::map<CullFace, D3D12_CULL_MODE> CullMap = {
	{CullFace::NONE, D3D12_CULL_MODE_NONE},
	{CullFace::FRONT, D3D12_CULL_MODE_FRONT},
	{CullFace::BACK, D3D12_CULL_MODE_BACK},
	//{CullFace::FRONT_AND_BACK, D3D12_CULL_MODE_BACK} //Not support
};

const static std::map<DepthFunction, D3D12_COMPARISON_FUNC> ComparisonFuncMap = {
	{DepthFunction::EQUAL, D3D12_COMPARISON_FUNC_EQUAL},
	{DepthFunction::NOT_EQUAL, D3D12_COMPARISON_FUNC_NOT_EQUAL},
	{DepthFunction::ALWAYS, D3D12_COMPARISON_FUNC_ALWAYS},
	{DepthFunction::NEVER, D3D12_COMPARISON_FUNC_NEVER},
	{DepthFunction::LESS, D3D12_COMPARISON_FUNC_LESS},
	{DepthFunction::LESS_EQUAL, D3D12_COMPARISON_FUNC_LESS_EQUAL},
	{DepthFunction::GREATER, D3D12_COMPARISON_FUNC_GREATER},
	{DepthFunction::GREATER_EQUAL, D3D12_COMPARISON_FUNC_GREATER_EQUAL}
};

const static std::map<BlendMode, D3D12_BLEND> BlendMap = {
	//{BlendFactor::NONE, D3D12_BLEND_ONE},
	{BlendMode::ONE, D3D12_BLEND_ONE},
	{BlendMode::ZERO, D3D12_BLEND_ZERO},
	{BlendMode::SRC_COLOR, D3D12_BLEND_SRC_COLOR},
	{BlendMode::ONE_MINUS_SRC_COLOR, D3D12_BLEND_INV_SRC_COLOR},
	{BlendMode::SRC_ALPHA, D3D12_BLEND_SRC_ALPHA},
	{BlendMode::ONE_MINUS_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA},
	{BlendMode::DST_COLOR, D3D12_BLEND_DEST_COLOR},
	{BlendMode::ONE_MINUS_DST_COLOR, D3D12_BLEND_INV_DEST_COLOR},
	{BlendMode::DST_ALPHA, D3D12_BLEND_DEST_ALPHA},
	{BlendMode::ONE_MINUS_DST_ALPHA, D3D12_BLEND_INV_DEST_ALPHA},
	{BlendMode::CONSTANT_COLOR, D3D12_BLEND_BLEND_FACTOR},
	{BlendMode::ONE_MINUS_CONSTANT_COLOR, D3D12_BLEND_INV_BLEND_FACTOR},
	{BlendMode::CONSTANT_ALPHA, D3D12_BLEND_ALPHA_FACTOR},
	{BlendMode::ONE_MINUS_CONSTANT_ALPHA, D3D12_BLEND_INV_ALPHA_FACTOR},
};

const static std::map<BlendFunction, D3D12_BLEND_OP> BlendOpMap = {
	{BlendFunction::ADD, D3D12_BLEND_OP_ADD},
	{BlendFunction::SUB, D3D12_BLEND_OP_SUBTRACT},
	{BlendFunction::MIN, D3D12_BLEND_OP_MIN},
	{BlendFunction::MAX, D3D12_BLEND_OP_MAX},
	{BlendFunction::REVERT_SUB, D3D12_BLEND_OP_REV_SUBTRACT},
};

const static std::map<StencilOperation, D3D12_STENCIL_OP> StencilOperationMap = {
	{StencilOperation::KEEP, D3D12_STENCIL_OP_KEEP},
	{StencilOperation::ZERO, D3D12_STENCIL_OP_ZERO},
	{StencilOperation::INCREMENT, D3D12_STENCIL_OP_INCR_SAT},
	{StencilOperation::INCREMENT_WRAP, D3D12_STENCIL_OP_INCR_SAT},
	{StencilOperation::DECREMENT, D3D12_STENCIL_OP_DECR},
	{StencilOperation::DECREMENT_WRAP, D3D12_STENCIL_OP_DECR_SAT},
	{StencilOperation::INVERT, D3D12_STENCIL_OP_INVERT},
	{StencilOperation::REPLACE, D3D12_STENCIL_OP_REPLACE},
};

std::string DriverDx12::State::getId() const {
	std::string res;
	res += std::to_string(mShader->getId());
	res += ':';
	if (mFrameBuffer) {
		res += std::to_string(mFrameBuffer->getId());
	}
	else {
		res += '-';
	}
	res += ':';
	res += (char)mCullFace;
	res += (char)mTriangleOrientation;
	res += (char)mRasterization;
	if (mDepth) {
		res += mDepth->mWriteMask;
		res += (char)mDepth->mFunc;
	}
	else {
		res += "--";
	}
	if (mBlend) {
		res += (char)mBlend->mColorFunc;
		res += (char)mBlend->mColorSrc;
		res += (char)mBlend->mColorDst;
		res += (char)mBlend->mAlphaFunc;
		res += (char)mBlend->mAlphaSrc;
		res += (char)mBlend->mAlphaDst;
		res += (char)mBlend->mColorMask;
	}
	else {
		res += "-------";
	}
	if (mStencil) {
		res += mStencil->mReadMask;
		res += (char)mStencil->mWriteMask;
		res += (char)mStencil->mDepthFail;
		res += (char)mStencil->mFail;
		res += (char)mStencil->mFunc;
		res += (char)mStencil->mPass;
	}
	else {
		res += "------";
	}
	return res;
}

std::vector<CD3DX12_STATIC_SAMPLER_DESC> DriverDx12::GetStaticSamplers() {
	auto make = [](UINT shaderRegister, D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE address, UINT maxAnisotropy = 1) {
		return CD3DX12_STATIC_SAMPLER_DESC(
			shaderRegister,
			filter,
			address,
			address,
			address,
			0.0f,
			maxAnisotropy,
			D3D12_COMPARISON_FUNC_LESS_EQUAL,
			D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
			0.0f,
			D3D12_FLOAT32_MAX);
	};

	std::vector<CD3DX12_STATIC_SAMPLER_DESC> samplers = {
		make(0, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP),
		make(1, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
		make(2, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP),
		make(3, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
		make(4, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 8),
		make(5, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 8),
	};
	for (UINT shaderRegister = 6; shaderRegister < 16; ++shaderRegister) {
		samplers.push_back(make(shaderRegister, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP));
	}
	return samplers;
}

DriverDx12::DriverDx12() {
	d3dUtil::mDriver = this;
    initializeStreamline();
	DriverDx12::init();
}

DriverDx12::~DriverDx12() {
#if IKIGAI_ENABLE_MEMORY_TRACKING
    if (mMemorySampler) mMemorySampler->disable();
#endif
	DriverDx12::end();
	wait();
	// Readback buffers hold D3D12MA allocations: release them while the allocator exists.
	mPendingReadbacks.clear();
	mFreeReadbacks.clear();
    mShuttingDown=true;
    mFrameGeneration.reset();
    #ifdef IKIGAI_USE_STREAMLINE
    mCurrentState={};mStates.clear();
    for(auto& [_,ctx]:mSwapchains){
        for(auto& framebuffer:ctx.mDefaultFb)framebuffer.reset();
        clearForDestroy();ctx.mSwapChain.Reset();
    }
    if(mDlssRuntime)std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->close();
    mCommandQueue.Reset();mStreamlineQueueProxy.Reset();mStreamlineDeviceProxy.Reset();
    #endif
    mImGuiTextures.clear();
	mVertexBuffer.reset();
    mBindlessFrameResources.clear();mBindlessTables.clear();mVertexStreams.clear();
	mIndexBuffer.reset();
	mUniformBuffers.clear();
	mStorageBuffers.clear();
	mTextures.clear();
	mStorageImageBindings.clear();
	mCurrentState = {};
	mStates.clear();
	mSwapchains.clear();
	clearForDestroy();
	mUploadRing.reset();
	mImmediateSubmit.reset();
    mDlssRuntime.reset();
    D3D12MA::TotalStatistics rtStats{};mAllocator->CalculateStatistics(&rtStats);
    std::cout<<"D3D12MA shutdown: allocations="<<rtStats.Total.Stats.AllocationCount<<" bytes="<<rtStats.Total.Stats.AllocationBytes<<"\n";
	mAllocator.Reset();
    Microsoft::WRL::ComPtr<ID3D12InfoQueue1> callbacks;
    if(mDebugCallbackCookie&&SUCCEEDED(mDevice.As(&callbacks)))callbacks->UnregisterMessageCallback(mDebugCallbackCookie);
	if (mFenceEvent) CloseHandle(mFenceEvent);
}

PixelFormat DriverDx12::getFrameBufferFormat() const {
	return  mCurrentState.mFrameBuffer ? 
		std::static_pointer_cast<TextureDx12>(mCurrentState.mFrameBuffer->getTextures()[0])->getFormat() : PixelFormat::RGBA_INT;
}

void DriverDx12::init() {
#if defined(DEBUG) || defined(_DEBUG) 
	// dx12 debug layer.
	{
		Microsoft::WRL::ComPtr<ID3D12Debug6> debugController;
		ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
		debugController->EnableDebugLayer();
		debugController->SetForceLegacyBarrierValidation(true);
		debugController->SetEnableAutoName(true);
		debugController->SetEnableGPUBasedValidation(true);
		debugController->SetEnableSynchronizedCommandQueueValidation(true);
	}
#endif

	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mDxgiFactory)));

	Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
	constexpr auto gpuPreference = DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
	HRESULT hardwareResult = E_FAIL;
	if (SUCCEEDED(mDxgiFactory->EnumAdapterByGpuPreference(0, gpuPreference, IID_PPV_ARGS(&adapter)))) {
		hardwareResult = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(mDevice.GetAddressOf()));
	}

	if (FAILED(hardwareResult)) {
		ThrowIfFailed(mDxgiFactory->EnumAdapterByGpuPreference(0, gpuPreference, IID_PPV_ARGS(&adapter)));
		ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&mDevice)));
	}

    bindStreamlineDevice();
	D3D12MA::ALLOCATOR_DESC allocatorDesc{};
	allocatorDesc.pDevice = mDevice.Get();
	allocatorDesc.pAdapter = adapter.Get();
	allocatorDesc.Flags = static_cast<D3D12MA::ALLOCATOR_FLAGS>(D3D12MA_RECOMMENDED_ALLOCATOR_FLAGS);
	ThrowIfFailed(D3D12MA::CreateAllocator(&allocatorDesc, mAllocator.GetAddressOf()));
	mUploadRing = std::make_unique<UploadRingDx12>();
#if IKIGAI_ENABLE_MEMORY_TRACKING
    try {
        Microsoft::WRL::ComPtr<IDXGIAdapter3> budgetAdapter; adapter.As(&budgetAdapter);
        DXGI_ADAPTER_DESC description{}; adapter->GetDesc(&description);
        D3D12_FEATURE_DATA_ARCHITECTURE architecture{};
        const bool unified = SUCCEEDED(mDevice->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture))) && architecture.UMA;
        mMemorySampler = std::make_shared<PROFILER::GpuMemorySampler>(
            [allocator = mAllocator, device = mDevice, budgetAdapter, description, unified, sampleIndex = UINT{0}]() mutable {
                allocator->SetCurrentFrameIndex(++sampleIndex);
                D3D12MA::Budget budgets[2]{}; allocator->GetBudget(&budgets[0], &budgets[1]);
                std::vector<PROFILER::GpuHeapSnapshot> result;
                for (UINT segment = 0; segment < 2; ++segment) {
                    PROFILER::GpuHeapSnapshot heap;
                    heap.backend = PROFILER::GpuBackend::DirectX12; heap.device = reinterpret_cast<std::uintptr_t>(device.Get());
                    heap.heap = segment; heap.deviceLocal = segment == 0; heap.unifiedMemory = unified;
                    heap.capacityBytes = segment == 0 && !unified ? description.DedicatedVideoMemory : description.SharedSystemMemory;
                    if (segment == 1 && unified) heap.capacityBytes = 0;
                    heap.allocationBytes = budgets[segment].Stats.AllocationBytes;
                    heap.reservedBytes = budgets[segment].Stats.BlockBytes; heap.allocationCount = budgets[segment].Stats.AllocationCount;
                    heap.processUsageBytes = budgets[segment].UsageBytes; heap.budgetBytes = budgets[segment].BudgetBytes;
                    DXGI_QUERY_VIDEO_MEMORY_INFO os{};
                    if (budgetAdapter && SUCCEEDED(budgetAdapter->QueryVideoMemoryInfo(0,
                        segment == 0 ? DXGI_MEMORY_SEGMENT_GROUP_LOCAL : DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &os))) {
                        heap.processUsageBytes = os.CurrentUsage; heap.budgetBytes = os.Budget; heap.driverBudget = true;
                    }
                    result.push_back(heap);
                }
                return result;
            });
        PROFILER::GpuMemoryProfiler::instance().registerSampler(mMemorySampler);
    } catch (...) { mMemorySampler.reset(); }
#endif
    configureRayTracing();
	configureGpuFeatures();


#if defined(DEBUG) || defined(_DEBUG) 
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
	mDevice.As(&infoQueue);
	infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
	infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
	infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);

	std::vector denyList = {
		D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
		D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
		D3D12_MESSAGE_ID_DRAW_EMPTY_SCISSOR_RECTANGLE
	};

	D3D12_INFO_QUEUE_FILTER filter{};
	filter.DenyList.NumIDs = denyList.size();
	filter.DenyList.pIDList = denyList.data();
	infoQueue->AddStorageFilterEntries(&filter);
    Microsoft::WRL::ComPtr<ID3D12InfoQueue1> callbacks;
    if(SUCCEEDED(mDevice.As(&callbacks)))callbacks->RegisterMessageCallback(
        [](D3D12_MESSAGE_CATEGORY,D3D12_MESSAGE_SEVERITY severity,D3D12_MESSAGE_ID,const char* message,void*) {
#ifdef IKIGAI_USE_STREAMLINE
            // DLSS-G's initial queue wait uses zero. This legal, already
            // satisfied fence wait is diagnosed as a warning by the debug layer.
            if(severity==D3D12_MESSAGE_SEVERITY_WARNING&&std::string_view(message)=="ID3D12CommandQueue1::Wait: Fence values can never be less than zero, so waiting for a fence value of zero will always be satisfied"){
                std::cerr<<"DX12_SDK_DIAGNOSTIC initial fence zero wait\n";return;
            }
#endif
            if(severity<=D3D12_MESSAGE_SEVERITY_WARNING)std::cerr<<"DX12_VALIDATION "<<message<<"\n";
        },D3D12_MESSAGE_CALLBACK_FLAG_NONE,nullptr,&mDebugCallbackCookie);
#endif

	createCommandList();
	createTimestampQueries();
	createDescriptorHeaps();
	auto& win = RESOURCES::ServiceManager::Get<WINDOW::Window>();
	mWidth = win.getSize().x;
	mHeight = win.getSize().y;
	mCurrentWindowID = win.getId();
	SwapchainContextDx12 ctx;
	mSwapchains[mCurrentWindowID] = std::move(ctx);
	
	createSwapChain(mCurrentWindowID, mWidth, mHeight);

	ThrowIfFailed(mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));
	mFenceValue = 1;
	mFenceEvent = CreateEvent(nullptr, false, false, nullptr);
	assert(mFenceEvent != nullptr);

	// Check 4X MSAA quality support for our back buffer format.
	// All Direct3D 11 capable devices support 4X MSAA for all render 
	// target formats, so we only need to check quality support.
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format = DefaultTextureColorFormat;
	msQualityLevels.SampleCount = 4;
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 0;
	ThrowIfFailed(mDevice->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&msQualityLevels,
		sizeof(msQualityLevels)));

	m4xMsaaQuality = msQualityLevels.NumQualityLevels;
	assert(m4xMsaaQuality > 0 && "Unexpected MSAA quality level.");

#ifdef _DEBUG
	logAdapters();
#endif

	createDefaultFrameBuffer(mCurrentWindowID, mWidth, mHeight);
	begin();
}

void DriverDx12::createDescriptorHeaps() {
	constexpr UINT engineDescriptors = 1000;
	constexpr UINT imguiDescriptors = 64;
	D3D12_DESCRIPTOR_HEAP_DESC descriptor {
		.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		.NumDescriptors = engineDescriptors + imguiDescriptors + 65536,
		.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
	};
	mDevice->CreateDescriptorHeap(&descriptor, IID_PPV_ARGS(&mDescriptorHeap));
    D3D12_DESCRIPTOR_HEAP_DESC samplers{};samplers.Type=D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplers.NumDescriptors=2048;samplers.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&samplers,IID_PPV_ARGS(&mBindlessSamplerHeap)));
	mDescriptorIncSize = mDevice->GetDescriptorHandleIncrementSize(descriptor.Type);
	mDescriptorHeapCPUHandle = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	mDescriptorHeapGPUHandle = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

	mImGuiFreeSrv.clear();
	mImGuiFreeSrv.reserve(imguiDescriptors);
	for (UINT index = engineDescriptors + imguiDescriptors; index > engineDescriptors; --index) {
		mImGuiFreeSrv.push_back(index - 1);
	}
}

UploadRingDx12& DriverDx12::getUploadRing() {
	return *mUploadRing;
}

void DriverDx12::immediateSubmit(const std::function<void(ID3D12GraphicsCommandList*)>& record) {
	if (!mImmediateSubmit) {
		mImmediateSubmit = std::make_unique<ImmediateSubmitDx12>(mDevice.Get());
	}
	mImmediateSubmit->submit(record);
}

void DriverDx12::allocImGuiSrv(D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
	assert(outCpu && outGpu);
	assert(!mImGuiFreeSrv.empty());
	const UINT index = mImGuiFreeSrv.back();
	mImGuiFreeSrv.pop_back();
	D3D12_CPU_DESCRIPTOR_HANDLE cpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE gpu = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	cpu.ptr += static_cast<SIZE_T>(index) * mDescriptorIncSize;
	gpu.ptr += static_cast<UINT64>(index) * mDescriptorIncSize;
	*outCpu = cpu;
	*outGpu = gpu;
}

void DriverDx12::freeImGuiSrv(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
	const D3D12_CPU_DESCRIPTOR_HANDLE start = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	const UINT index = static_cast<UINT>((cpu.ptr - start.ptr) / mDescriptorIncSize);
	mImGuiFreeSrv.push_back(index);
}

void DriverDx12::executeRecordedCommands() {
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* lists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(1, lists);
	// The allocator still backs the executed list; Reset on the list alone only reopens recording.
	ThrowIfFailed(mCommandList->Reset(mCommandAllocator.Get(), nullptr));
	for (auto flag :
		{ Dirty::VIEWPORT, Dirty::SCISSOR, Dirty::PRIMITIVE_MODE, Dirty::RASTERIZATION_MODE, Dirty::VERTEX_BUFFER, Dirty::INDEX_BUFFER }) {
		setDirty(flag);
	}
}

void DriverDx12::createCommandList() {
	D3D12_COMMAND_QUEUE_DESC descriptor{
		.Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
		.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE
	};
    #ifdef IKIGAI_USE_STREAMLINE
    if(mDlssRuntime){
        auto runtime=std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime);
        void* device=mDevice.Get();mDevice->AddRef();
        StreamlineRuntime::check(runtime->upgrade(&device),"slUpgradeInterface device");
        mStreamlineDeviceProxy.Attach(static_cast<ID3D12Device*>(device));
        ThrowIfFailed(mStreamlineDeviceProxy->CreateCommandQueue(&descriptor,IID_PPV_ARGS(&mStreamlineQueueProxy)));
        void* nativeQueue=nullptr;
        StreamlineRuntime::check(runtime->nativeInterface(mStreamlineQueueProxy.Get(),&nativeQueue),"slGetNativeInterface queue");
        mCommandQueue.Attach(static_cast<ID3D12CommandQueue*>(nativeQueue));
    }else
    #endif
	ThrowIfFailed(mDevice->CreateCommandQueue(&descriptor, IID_PPV_ARGS(&mCommandQueue)));
	ThrowIfFailed(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(mCommandAllocator.GetAddressOf())));
	ThrowIfFailed(mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mCommandAllocator.Get(), nullptr, IID_PPV_ARGS(mCommandList.GetAddressOf())));
	mCommandList->Close();
}

void DriverDx12::setVertexBuffer(std::shared_ptr<VertexBufferInterface> buffer) {
	resetVertexStreams();
    mVertexBuffer = std::static_pointer_cast<VertexBufferDx12>(buffer);
	setDirty(Dirty::VERTEX_BUFFER);
}

void DriverDx12::setIndexBuffer(std::shared_ptr<IndexBufferInterface> buffer) {
	mIndexBuffer = std::static_pointer_cast<IndexBufferDx12>(buffer);
	setDirty(Dirty::INDEX_BUFFER);
}

void DriverDx12::setShader(std::shared_ptr<ShaderInterface> shader) {
	mCurrentState.mShader = std::static_pointer_cast<ShaderDx12>(shader);
	mPendingPushConstants.clear();
}

void DriverDx12::flushPushConstants(bool compute) {
	auto shader = mCurrentState.mShader;
	if (!shader || shader->mPushConstantRootIndex < 0 || shader->mPushConstantDwords == 0 || !mCommandList) {
		mPendingPushConstants.clear();
		return;
	}
	const auto rootIndex = static_cast<UINT>(shader->mPushConstantRootIndex);
	for (const auto& write : mPendingPushConstants) {
		if (write.values.empty() || write.offsetDwords >= shader->mPushConstantDwords) {
			continue;
		}
		const auto count = static_cast<UINT>(std::min<size_t>(write.values.size(), shader->mPushConstantDwords - write.offsetDwords));
		if (compute) {
			mCommandList->SetComputeRoot32BitConstants(rootIndex, count, write.values.data(), write.offsetDwords);
		} else {
			mCommandList->SetGraphicsRoot32BitConstants(rootIndex, count, write.values.data(), write.offsetDwords);
		}
	}
	mPendingPushConstants.clear();
}

void DriverDx12::resize(size_t width, size_t height) {
    if(width==0||height==0)return;
    const auto& current=mSwapchains.at(mCurrentWindowID);
    if(current.width==width&&current.height==height)return;
	mWidth = static_cast<unsigned>(width);
	mHeight = static_cast<unsigned>(height);
	onResize();
}

void DriverDx12::onResize() {
    if(mFrameGeneration)mFrameGeneration->suspend();
	end();
    wait();
    if(mFrameGeneration)mFrameGeneration->waitForPresents();
	auto& ctx = mSwapchains[mCurrentWindowID];
	mCurrentState.mFrameBuffer.reset();
	for (UINT i = 0; i < DEFAULT_FB_SIZE; i++) {
		ctx.mDefaultFb[i].reset();
	}
	wait();
	clearForDestroy();
	ThrowIfFailed(ctx.mSwapChain->ResizeBuffers(DEFAULT_FB_SIZE, (UINT)mWidth, (UINT)mHeight, DefaultTextureColorFormat, 0));
	ctx.width=mWidth;ctx.height=mHeight;
	createDefaultFrameBuffer(mCurrentWindowID, mWidth, mHeight);
	begin();

	if (!mViewport) {
		setDirty(Dirty::VIEWPORT);
	}
	if (mScissor) {
		setDirty(Dirty::SCISSOR);
	}
}

void DriverDx12::setPrimitiveMode(PrimitiveMode param) {
	mPrimitiveMode = param;
	setDirty(Dirty::PRIMITIVE_MODE);
}

void DriverDx12::setRasterization(RasterizationMode param) {
	mCurrentState.mRasterization = param;
	setDirty(Dirty::RASTERIZATION_MODE);
}

void DriverDx12::setViewport(const Viewport& param) {
	mViewport = param;
	setDirty(Dirty::VIEWPORT);
}

void DriverDx12::resetViewport() {
	mViewport = std::nullopt;
	setDirty(Dirty::VIEWPORT);
}

void DriverDx12::setScissor(const Scissor& param) {
	mScissor = param;
	setDirty(Dirty::SCISSOR);
}

void DriverDx12::resetScissor() {
	mScissor = std::nullopt;
	setDirty(Dirty::SCISSOR);
}

void DriverDx12::setBlending(const Blending& param) {
	mCurrentState.mBlend = param;
}

void DriverDx12::resetBlending() {
	mCurrentState.mBlend = std::nullopt;
}

void DriverDx12::setDepth(const Depth& param) {
	mCurrentState.mDepth = param;
}

void DriverDx12::resetDepth() {
	mCurrentState.mDepth = std::nullopt;
}

void DriverDx12::setStencil(const Stencil& param) {
	mCurrentState.mStencil = param;
}

void DriverDx12::resetStencil() {
	mCurrentState.mStencil = std::nullopt;
}

void DriverDx12::setCull(CullFace param) {
	mCurrentState.mCullFace = param;
}

void DriverDx12::setMSAA(bool value) {
	if (m4xMsaaState != value) {
		m4xMsaaState = value;

		// Recreate the swapchain and buffers with new multisample settings.
		createSwapChain(mCurrentWindowID, mWidth, mHeight);
		onResize();
	}
}

void DriverDx12::setPushConstant(ShaderType stage, uint32_t offset, uint32_t size, const void* data) {
	if (!mCurrentState.mShader || mCurrentState.mShader->mPushConstantRootIndex < 0 || !data || size < 4) {
		return;
	}
	const auto dwordOffset = offset / 4;
	const auto dwordCount = size / 4;
	if (dwordCount == 0 || dwordOffset >= mCurrentState.mShader->mPushConstantDwords) {
		return;
	}
	PendingPushConstant write;
	write.offsetDwords = dwordOffset;
	write.values.resize(dwordCount);
	std::memcpy(write.values.data(), data, dwordCount * sizeof(uint32_t));
	mPendingPushConstants.push_back(std::move(write));
	(void)stage;
}

bool DriverDx12::get4xMsaaState() const {
	return m4xMsaaState;
}

void DriverDx12::setClearColor(const MATH::Vector4f& color) {
	mClearColor = color;
}

void DriverDx12::setClearColor(float r, float g, float b, float a) {
	mClearColor = {r, g, b, a};
}

void DriverDx12::clear(bool clearColor, bool clearDepth, bool clearStencil) {
	std::shared_ptr<FrameBufferDx12> fb = mCurrentState.mFrameBuffer ? mCurrentState.mFrameBuffer : getDefaultFrameBuffer();
	for (auto& texture : fb->getTextures()) {
		std::static_pointer_cast<TextureDx12>(texture)->setState(mCommandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
	}
	if (fb->getDepth()) std::static_pointer_cast<TextureDx12>(fb->getDepth())->setState(mCommandList.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

	auto dsv_descriptor = fb->getDepthHeap()->GetCPUDescriptorHandleForHeapStart();

	if (clearColor) {
		CD3DX12_CPU_DESCRIPTOR_HANDLE heapStart;
		heapStart = fb->getTexHeap()->GetCPUDescriptorHandleForHeapStart();
		auto incSize = d3dUtil::GetDriver()->getDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		for (auto& texture : fb->getTextures()) {
			mCommandList->ClearRenderTargetView(heapStart, (float*)&mClearColor, 0, nullptr);
			heapStart.Offset(1, incSize);
		}
	}

	if (clearDepth || clearStencil) {
		D3D12_CLEAR_FLAGS flags = {};
		if (clearDepth) {
			flags |= D3D12_CLEAR_FLAG_DEPTH;
		}
		if (clearStencil) {
			flags |= D3D12_CLEAR_FLAG_STENCIL;
		}
		mCommandList->ClearDepthStencilView(dsv_descriptor, flags, mDepthValue,
			mStencilValue, 0, nullptr);
	}
}

void DriverDx12::draw(uint32_t vertex_count, uint32_t vertex_offset, uint32_t instance_count) {
    if (!vertex_count || !instance_count) return;
    validateVertexDraw({vertex_count,vertex_offset,instance_count});
	applyState();
	mCommandList->DrawInstanced((UINT)vertex_count, (UINT)instance_count, (UINT)vertex_offset, 0);
}

std::vector<float> DriverDx12::readTexture(std::shared_ptr<TextureInterface> texture,unsigned mip,unsigned layer) {
    const bool unorm=texture->getFormat()==PixelFormat::RGBA_INT;
	const bool packed = IsPackedFloatFormat(texture->getFormat());
	if (!unorm && !packed && texture->getFormat() != PixelFormat::RGBA_FLOAT && texture->getFormat() != PixelFormat::RG_FLOAT
		&& texture->getFormat() != PixelFormat::R_FLOAT && texture->getType() != TextureType::DEPTH) {
		throw std::runtime_error("Unsupported DX12 readback format");
	}
    auto t=std::static_pointer_cast<TextureDx12>(texture);auto resource=t->getResource().Get();auto desc=resource->GetDesc();
	const bool arrayed = t->getType() == TextureType::TEXTURE_CUBE || t->getType() == TextureType::TEXTURE_2D_ARRAY;
	const UINT subresource = mip + (arrayed ? layer * t->getMipCount() : 0);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 rowSize=0,bytes=0;
    mDevice->GetCopyableFootprints(&desc,subresource,1,0,&footprint,&rows,&rowSize,&bytes);
    auto buffer=d3dUtil::CreateBuffer(bytes,D3D12_HEAP_TYPE_READBACK);
    const auto previousState=t->getState();
    d3dUtil::OneTimeSubmit([&](ID3D12GraphicsCommandList* cmd) {
        t->setState(cmd,D3D12_RESOURCE_STATE_COPY_SOURCE);
        auto source=CD3DX12_TEXTURE_COPY_LOCATION(resource,subresource);
        auto destination=CD3DX12_TEXTURE_COPY_LOCATION(buffer.Get(),footprint);
        cmd->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
        t->setState(cmd,previousState);
    });
	const size_t channels = packed				  ? PackedFloatReadbackChannels(t->getFormat())
		: t->getType() == TextureType::DEPTH	  ? 1
		: t->getFormat() == PixelFormat::RG_FLOAT ? 2
												  : 4,
				 w = std::max<size_t>(1, t->getWidth() >> mip), h = std::max<size_t>(1, t->getHeight() >> mip);
    std::vector<float> values(w*h*channels);void* mapped=nullptr;D3D12_RANGE range{0,static_cast<SIZE_T>(bytes)};
    ThrowIfFailed(buffer.Get()->Map(0,&range,&mapped));
    const auto* src=static_cast<const uint8_t*>(mapped)+footprint.Offset+(t->getType()==TextureType::TEXTURE_3D?layer*h*footprint.Footprint.RowPitch:0);
    const bool red=t->getFormat()==PixelFormat::R_FLOAT && t->getType()!=TextureType::DEPTH;
    for(size_t y=0;y<h;++y) {
		if (packed) {
			DecodePackedFloatTexels(t->getFormat(), src + y * footprint.Footprint.RowPitch, w, values.data() + y * w * channels);
		}
		else if (unorm) {
			for (size_t x = 0; x < w * 4; ++x) {
				values[y * w * 4 + x] = src[y * footprint.Footprint.RowPitch + x] / 255.f;
			}
		}
        else if(red) for(size_t x=0;x<w;++x) {
            float r;std::memcpy(&r,src+y*footprint.Footprint.RowPitch+x*sizeof(float),sizeof(float));
            values[(y*w+x)*4]=r;values[(y*w+x)*4+3]=1;
        } else std::memcpy(values.data()+y*w*channels,src+y*footprint.Footprint.RowPitch,w*channels*sizeof(float));
    }
    D3D12_RANGE written{0,0};buffer.Get()->Unmap(0,&written);return values;
}

void DriverDx12::dispatch(uint32_t x,uint32_t y,uint32_t z) {
    applyState(); mCommandList->Dispatch(x,y,z);
    auto barrier=CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
    mCommandList->ResourceBarrier(1,&barrier);
}

void DriverDx12::renderGraphBarrier(bool uav) {
	if (uav) {
		const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
		mCommandList->ResourceBarrier(1, &barrier);
	}
}

void DriverDx12::drawIndexed(uint32_t index_count, uint32_t index_offset, uint32_t instance_count) {
    if (!index_count || !instance_count) return;
    validateVertexDraw({index_count,index_offset,instance_count,0,0,true});
	applyState();
	mCommandList->DrawIndexedInstanced((UINT)index_count, (UINT)instance_count, (UINT)index_offset, 0, 0);
}

void DriverDx12::draw(const MeshInterface& mesh, PrimitiveMode primitive, uint32_t instances) {
	if (instances > 0) {
		mFrameInfo.mBatchCount++;
		mFrameInfo.mInstanceCount += instances;
		if (mesh.getIndexCount() > 0) {
			mFrameInfo.mPolyCount += (mesh.getIndexCount() / 3) * instances;
		}
		else {
			mFrameInfo.mPolyCount += (mesh.getVertexCount() / 3) * instances;
		}

		setPrimitiveMode(primitive);
		mesh.bind();
        validateVertexDraw({static_cast<uint32_t>(mesh.getIndexCount()?mesh.getIndexCount():mesh.getVertexCount()),0,instances,0,0,mesh.getIndexCount()>0});
		applyState();
		if (mesh.getIndexCount() > 0) {
			mCommandList->DrawIndexedInstanced(mesh.getIndexCount(), instances, mesh.getOffset(), 0, 0);
		}
		else {
			mCommandList->DrawInstanced(mesh.getVertexCount(), instances, mesh.getOffset(), 0);
		}
		mesh.unbind();
	}
}

Microsoft::WRL::ComPtr<ID3D12Device> DriverDx12::getDevice() {
	return mDevice;
}

Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> DriverDx12::getCommandList() {
	return mCommandList;
}

void DriverDx12::submit() {
	presentFrame();
	waitForPresentedFrame();
	finishPresent();
}

void DriverDx12::presentFrame() {
	// Defer transitions until after all graph passes, including RT/UAV writes.
	// This list deliberately survives resize waits before the UI is submitted.
	for (auto& texture : mImGuiTextures) {
		texture->setState(mCommandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		mRayFrameResources.push_back(std::move(texture));
	}
	mImGuiTextures.clear();
	mCommandList->SetDescriptorHeaps(1, mDescriptorHeap.GetAddressOf());
	if (!mPresentationUIRendered) {
		if (auto* imgui = IKIGAI::IMGUI::Get()) {
			imgui->renderDrawData();
		}
	}

	end();
	if (CAPTURE::ScreenshotService::Get().pending()) {
		// Diagnostic readTexture uses a separate queue: finish rendering first.
		ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mFenceValue));
		ThrowIfFailed(mFence->SetEventOnCompletion(mFenceValue++, mFenceEvent));
		WaitWithWindowMessages(mFenceEvent);
		CAPTURE::ScreenshotService::Get().captureTexture(*this, getDefaultFrameBuffer()->getTextures().at(0));
	}
	bool vsync = settings.frameGeneration.vsync;
	if (settings.presentMode != RenderSettings::PresentMode::DEFAULT) {
		// No tearing flag: MAILBOX and IMMEDIATE both present with sync interval 0.
		vsync = settings.presentMode == RenderSettings::PresentMode::FIFO;
	}
	latencySubmitEnd();
#if defined(IKIGAI_USE_STREAMLINE) && (defined(DEBUG) || defined(_DEBUG))
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> sdkDiagnostics;
	if (mSwapchains.at(mCurrentWindowID).streamline && SUCCEEDED(mDevice.As(&sdkDiagnostics))) {
		sdkDiagnostics->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);
	}
#endif
#ifdef IKIGAI_USE_STREAMLINE
	if (mDlssRuntime) {
		std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->marker(sl::PCLMarker::ePresentStart);
	}
	if (mDlssRuntime && !mSwapchains.at(mCurrentWindowID).streamline) {
		std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->commonPresentBefore();
	}
#endif
	mSwapchains[mCurrentWindowID].mSwapChain->Present(vsync ? 1 : 0, 0);
#ifdef IKIGAI_USE_STREAMLINE
	if (mDlssRuntime && !mSwapchains.at(mCurrentWindowID).streamline) {
		std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->commonPresentAfter();
	}
	if (mDlssRuntime) {
		std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->marker(sl::PCLMarker::ePresentEnd);
	}
#endif
	mFrameId = mSwapchains[mCurrentWindowID].mSwapChain->GetCurrentBackBufferIndex();
}

void DriverDx12::finishPresent() {
	releaseCompletedFrame();
	if (mFrameGeneration) {
		mFrameGeneration->waitForPresents();
		mFrameGeneration->afterPresent();
		mFrameGenerationStatus = mFrameGeneration->status();
	}
#if defined(IKIGAI_USE_STREAMLINE) && (defined(DEBUG) || defined(_DEBUG))
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> sdkDiagnostics;
	if (mSwapchains.at(mCurrentWindowID).streamline && SUCCEEDED(mDevice.As(&sdkDiagnostics))) {
		sdkDiagnostics->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
	}
#endif
	begin();
}

void DriverDx12::begin() {
    mBindlessSnapshots.clear();mBindlessFrameResources.clear();
    mBindlessDescriptorCursor=1064;mBindlessSamplerCursor=0;
	mUploadRing->beginFrame(mFence ? mFence->GetCompletedValue() : 0);
	beginCommandList(mCommandAllocator.Get(), mCommandList.Get());

	setDirty(Dirty::VIEWPORT);
	setDirty(Dirty::SCISSOR);
	setDirty(Dirty::PRIMITIVE_MODE);
	setDirty(Dirty::RASTERIZATION_MODE);
	setDirty(Dirty::VERTEX_BUFFER);
	setDirty(Dirty::INDEX_BUFFER);
}

void DriverDx12::end() {
	if (!mRecording) {
		return;
	}
	std::static_pointer_cast<TextureDx12>(getDefaultFrameBuffer()->getTextures()[0])->setState(mCommandList.Get(), D3D12_RESOURCE_STATE_PRESENT);
	const bool timestamps = resolveTimestampQueries();
	endCommandList(mCommandQueue.Get(), mCommandList.Get(), false);
	completionSource()->submitted();
	// Every later signal of mFence on this queue follows the submission above.
	mUploadRing->retireFrame(mFenceValue);
	if (timestamps) {
		signalTimestampQueries();
	}
}

void DriverDx12::beginCommandList(ID3D12CommandAllocator* cmdAlloc, ID3D12GraphicsCommandList* cmdList) {
	ThrowIfFailed(cmdAlloc->Reset());
	ThrowIfFailed(cmdList->Reset(cmdAlloc, nullptr));
	mRecording = true;
}

void DriverDx12::endCommandList(ID3D12CommandQueue* cmdQueue, ID3D12GraphicsCommandList* cmdList, bool needWait) {
	ThrowIfFailed(cmdList->Close());
	mRecording = false;
	cmdQueue->ExecuteCommandLists(1, CommandListCast(&cmdList));
	if (needWait) {
		Microsoft::WRL::ComPtr<ID3D12Fence> fence;
		ThrowIfFailed(mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())));

		DirectX::SetDebugObjectName(fence.Get(), L"ResourceUploadBatch");

		const auto event = CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);

		ThrowIfFailed(cmdQueue->Signal(fence.Get(), 1ULL));
		ThrowIfFailed(fence->SetEventOnCompletion(1ULL, event));

		WaitForSingleObject(event, INFINITE);
		CloseHandle(event);
	}
}

void DriverDx12::setTexture(size_t bind, std::shared_ptr<TextureInterface> data) {
	mTextures[bind] = std::static_pointer_cast<TextureDx12>(data);
	mStorageImageBindings.erase(bind);
}

void DriverDx12::setUniformBuffer(size_t bind, std::shared_ptr<UniformBufferInterface> data) {
	mUniformBuffers[bind] = std::static_pointer_cast<UniformBufferDx12>(data);
}

void DriverDx12::setStorageBuffer(size_t bind, std::shared_ptr<StorageBufferInterface> data) {
	mStorageBuffers[bind] = std::static_pointer_cast<StorageBufferDx12>(data);
}

void DriverDx12::setTexture(const std::string& name, std::shared_ptr<TextureInterface> data) {
	const auto& reflection = mCurrentState.mShader->getReflection();
	const auto bind = reflection.mUniforms[reflection.mNameToUniforms.at(name)].mBind;
	mTextures[bind] = std::static_pointer_cast<TextureDx12>(data);
	mStorageImageBindings.erase(bind);
}

void DriverDx12::setUniformBuffer(const std::string& name, std::shared_ptr<UniformBufferInterface> data) {
	const auto& reflection = mCurrentState.mShader->getReflection();
	const auto bind = reflection.mUniforms[reflection.mNameToUniforms.at(name)].mBind;
	mUniformBuffers[bind] = std::static_pointer_cast<UniformBufferDx12>(data);
}

void DriverDx12::setStorageBuffer(const std::string& name, std::shared_ptr<StorageBufferInterface> data) {
	const auto& reflection = mCurrentState.mShader->getReflection();
	const auto bind = reflection.mUniforms[reflection.mNameToUniforms.at(name)].mBind;
	mStorageBuffers[bind] = std::static_pointer_cast<StorageBufferDx12>(data);
}

void DriverDx12::createSwapChain(unsigned int windowID, unsigned width, unsigned height, bool streamline) {
	auto& ctx = mSwapchains[windowID];
	// Release the previous swapchain we will be recreating.
	ctx.mSwapChain.Reset();

	auto& win = RESOURCES::ServiceManager::Get<WINDOW::Window>();
	ctx.width = width;
	ctx.height = height;

	DXGI_SWAP_CHAIN_DESC1 descriptor{};
	descriptor.BufferCount = DEFAULT_FB_SIZE;
	descriptor.Width = ctx.width;
	descriptor.Height = ctx.height;
	descriptor.Format = DefaultTextureColorFormat;
	descriptor.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	descriptor.SampleDesc.Count = 1;
	descriptor.SampleDesc.Quality = 0;
	descriptor.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	descriptor.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	descriptor.Scaling = DXGI_SCALING_NONE;
	descriptor.Stereo = false;

	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsDescriptor = {0};
	fsDescriptor.Windowed = true;

	Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain;
    auto* presentationQueue=mCommandQueue.Get();
#ifdef IKIGAI_USE_STREAMLINE
    void* nativeQueue=nullptr;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> nativeQueueOwner;
    if(mDlssRuntime&&!streamline&&std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->nativeInterface(mCommandQueue.Get(),&nativeQueue)==sl::Result::eOk){
        nativeQueueOwner.Attach(static_cast<ID3D12CommandQueue*>(nativeQueue));presentationQueue=nativeQueueOwner.Get();
    }
    if(streamline&&mDlssRuntime){
        presentationQueue=mStreamlineQueueProxy.Get();
        auto runtime=std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime);
        void* factory=mDxgiFactory.Get();mDxgiFactory->AddRef();
        const auto result=runtime->upgrade(&factory);
        Microsoft::WRL::ComPtr<IDXGIFactory4> proxy;proxy.Attach(static_cast<IDXGIFactory4*>(factory));
        StreamlineRuntime::check(result,"slUpgradeInterface factory");
        ThrowIfFailed(proxy->CreateSwapChainForHwnd(presentationQueue,win.getContextDX12(),&descriptor,&fsDescriptor,nullptr,swapchain.GetAddressOf()));
        ThrowIfFailed(swapchain.As(&ctx.mSwapChain));return;
    }
#endif
    ThrowIfFailed(mDxgiFactory->CreateSwapChainForHwnd(presentationQueue, win.getContextDX12(),
		&descriptor, &fsDescriptor, nullptr, swapchain.GetAddressOf()));

    ThrowIfFailed(swapchain.As(&ctx.mSwapChain));
}

void DriverDx12::destroyDeferred(Microsoft::WRL::ComPtr<ID3D12DeviceChild> object) {
    MarkGpuMemoryPendingDx12(object.Get());
	mDestroyDeffered.push_back(object);
}

void DriverDx12::destroyDeferred(Dx12Resource object) {
    MarkGpuMemoryPendingDx12(object.Get());
	mDestroyDeferredResources.push_back(std::move(object));
}

std::vector<Microsoft::WRL::ComPtr<ID3D12DeviceChild>>& DriverDx12::getDestroyDeferredObjects() {
	return mDestroyDeffered;
}

uint32_t DriverDx12::allocateLegacyDescriptors(uint32_t count) {
	if (count == 0 || count >= mFreeLegacyDescriptors.size()) {
		throw std::runtime_error("Unsupported legacy descriptor run length");
	}
	auto& free = mFreeLegacyDescriptors[count];
	if (!free.empty()) {
		const uint32_t index = free.back();
		free.pop_back();
		return index;
	}
	if (mLegacyDescriptorCursor + count > LegacyDescriptorCapacity) {
		throw std::runtime_error("Legacy descriptor range exhausted; bindless/ImGui ranges are reserved");
	}
	const uint32_t index = mLegacyDescriptorCursor;
	mLegacyDescriptorCursor += count;
	return index;
}

void DriverDx12::releaseLegacyDescriptors(uint32_t index, uint32_t count) {
	if (index != InvalidLegacyDescriptor) {
		mPendingLegacyDescriptors.emplace_back(index, count);
	}
}

D3D12_CPU_DESCRIPTOR_HANDLE DriverDx12::legacyDescriptorCpu(uint32_t index) const {
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(mDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(index), mDescriptorIncSize);
}

D3D12_GPU_DESCRIPTOR_HANDLE DriverDx12::legacyDescriptorGpu(uint32_t index) const {
	return CD3DX12_GPU_DESCRIPTOR_HANDLE(mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), static_cast<INT>(index), mDescriptorIncSize);
}

void DriverDx12::clearForDestroy() {
	for (const auto& [index, count] : mPendingLegacyDescriptors) {
		mFreeLegacyDescriptors[count].push_back(index);
	}
	mPendingLegacyDescriptors.clear();
	mRayFrameResources.clear();
    while(!mRayDeferred.empty()||!mDestroyDeffered.empty()||!mDestroyDeferredResources.empty()) {
        auto ray=std::move(mRayDeferred);mRayDeferred.clear();
        auto objects=std::move(mDestroyDeffered);mDestroyDeffered.clear();
        auto resources=std::move(mDestroyDeferredResources);mDestroyDeferredResources.clear();
        ray.clear();objects.clear();resources.clear();
    }
}

std::shared_ptr<TextureInterface> DriverDx12::createTexture(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureDx12::Create(path, allocator, deleter);
}
std::shared_ptr<TextureInterface> DriverDx12::createTextureAtlas(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureAtlasDx12::CreateAtlas(path, generateMipmap, allocator, deleter);
}
std::shared_ptr<TextureInterface> DriverDx12::createTexture(const TextureResource& res, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureDx12::Create(res, allocator, deleter);
}
std::shared_ptr<TextureInterface> DriverDx12::createTexture(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureDx12::Create(res, fileData, allocator, deleter);
}
std::shared_ptr<TextureInterface> DriverDx12::createTextureAtlas(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureAtlasDx12::CreateAtlasFromResource(res, fileData, allocator, deleter);
}
std::shared_ptr<TextureInterface> DriverDx12::createTexture(const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	TextureResource res;
	res.path = name;
	res.useMipmap = generateMipmap;
	res.pathTexture.push_back(name);
	return TextureDx12::Create(res, std::vector<std::vector<uint8_t>> { data }, allocator, deleter);
}

void DriverDx12::wait() {
	waitForPresentedFrame();
	releaseCompletedFrame();
}

void DriverDx12::waitForPresentedFrame() {
	ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mFenceValue));
	ThrowIfFailed(mFence->SetEventOnCompletion(mFenceValue, mFenceEvent));
	WaitWithWindowMessages(mFenceEvent);
	++mFenceValue;
}

void DriverDx12::releaseCompletedFrame() {
	completionSource()->completed(completionSource()->lastSubmitted());
	clearForDestroy();
}

void DriverDx12::waitForCompletion(GpuCompletionToken token) {
	requireOwnerThread();
	completionSource()->validate(token);
	if (!completionSource()->isComplete(token)) {
		wait();
	}
}

void DriverDx12::logAdapters() {
	UINT i = 0;
	IDXGIAdapter* adapter = nullptr;
	std::vector<IDXGIAdapter*> adapterList;
	while (mDxgiFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND) {
		DXGI_ADAPTER_DESC desc;
		adapter->GetDesc(&desc);

		std::wstring text = L"***Adapter: ";
		text += desc.Description;
		text += L"\n";

		OutputDebugStringW(text.c_str());

		adapterList.push_back(adapter);

		++i;
	}

	for (size_t i = 0; i < adapterList.size(); ++i) {
		logAdapterOutputs(adapterList[i]);
		ReleaseCom(adapterList[i]);
	}
}

void DriverDx12::createDefaultFrameBuffer(unsigned int windowID, unsigned width, unsigned height) {
	auto& ctx = mSwapchains[windowID];
	for (UINT i = 0; i < DEFAULT_FB_SIZE; i++) {
		Microsoft::WRL::ComPtr<ID3D12Resource> backbuffer;
		ctx.mSwapChain->GetBuffer(i, IID_PPV_ARGS(backbuffer.GetAddressOf()));
		auto tex = IKIGAI::MEMORY::MakeShared<TextureDx12, IKIGAI::MEMORY::Category::Texture>(width, height, PixelFormat::RGBA_INT, backbuffer);
		auto fb = std::make_shared<FrameBufferDx12>(std::vector<std::shared_ptr<TextureInterface>>{tex}, nullptr);
		ctx.mDefaultFb[i] = fb;
	}

	ctx.width = width;
	ctx.height = height;
	mFrameId = ctx.mSwapChain->GetCurrentBackBufferIndex();
}

void DriverDx12::logAdapterOutputs(IDXGIAdapter* adapter) {
	UINT i = 0;
	IDXGIOutput* output = nullptr;
	while (adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND) {
		DXGI_OUTPUT_DESC desc;
		output->GetDesc(&desc);

		std::wstring text = L"***Output: ";
		text += desc.DeviceName;
		text += L"\n";
		OutputDebugStringW(text.c_str());

		logOutputDisplayModes(output, DefaultTextureColorFormat);

		ReleaseCom(output);

		++i;
	}
}

void DriverDx12::logOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format) {
	UINT count = 0;
	UINT flags = 0;

	// Call with nullptr to get list count.
	output->GetDisplayModeList(format, flags, &count, nullptr);

	std::vector<DXGI_MODE_DESC> modeList(count);
	output->GetDisplayModeList(format, flags, &count, &modeList[0]);

	for (auto& x : modeList) {
		UINT n = x.RefreshRate.Numerator;
		UINT d = x.RefreshRate.Denominator;
		std::wstring text =
			L"Width = " + std::to_wstring(x.Width) + L" " +
			L"Height = " + std::to_wstring(x.Height) + L" " +
			L"Refresh = " + std::to_wstring(n) + L"/" + std::to_wstring(d) +
			L"\n";
		OutputDebugStringW(text.c_str());
	}
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> DriverDx12::createNewState(const State& state) {
	auto depthMode = state.mDepth;
	if (!depthMode) {
		depthMode = Depth();
	}
	//const auto& blendMode = state.blend_mode;

	auto depthDescriptor = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	depthDescriptor.DepthEnable = state.mDepth.has_value();
	depthDescriptor.DepthWriteMask = depthMode->mWriteMask ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	depthDescriptor.DepthFunc = ComparisonFuncMap.at(depthMode->mFunc);

	const auto isStencil = state.mStencil.has_value();
	depthDescriptor.StencilEnable = isStencil;
	if (isStencil) {
		depthDescriptor.StencilReadMask = state.mStencil->mReadMask;
		depthDescriptor.StencilWriteMask = state.mStencil->mWriteMask;
		depthDescriptor.BackFace.StencilFailOp = StencilOperationMap.at(state.mStencil->mFail);
		depthDescriptor.BackFace.StencilDepthFailOp = StencilOperationMap.at(state.mStencil->mDepthFail);
		depthDescriptor.BackFace.StencilPassOp = StencilOperationMap.at(state.mStencil->mPass);
		depthDescriptor.BackFace.StencilFunc = ComparisonFuncMap.at(state.mStencil->mFunc);
	}

	auto blendDescriptor = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	blendDescriptor.AlphaToCoverageEnable = false;

	for (int i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
		auto& blend = blendDescriptor.RenderTarget[i];

		blend.BlendEnable = state.mBlend.has_value();

		if (!blend.BlendEnable)
			continue;

		const auto& blend_mode_nn = state.mBlend.value();

		if ((blend_mode_nn.mColorMask & Color::R) != Color::NONE)
			blend.RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_RED;

		if ((blend_mode_nn.mColorMask & Color::G) != Color::NONE)
			blend.RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_GREEN;

		if ((blend_mode_nn.mColorMask & Color::B) != Color::NONE)
			blend.RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_BLUE;

		if ((blend_mode_nn.mColorMask & Color::A) != Color::NONE)
			blend.RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_ALPHA;

		blend.SrcBlend = BlendMap.at(blend_mode_nn.mColorSrc);
		blend.DestBlend = BlendMap.at(blend_mode_nn.mColorDst);
		blend.BlendOp = BlendOpMap.at(blend_mode_nn.mColorFunc);

		blend.SrcBlendAlpha = BlendMap.at(blend_mode_nn.mAlphaSrc);
		blend.DestBlendAlpha = BlendMap.at(blend_mode_nn.mAlphaDst);
		blend.BlendOpAlpha = BlendOpMap.at(blend_mode_nn.mAlphaFunc);
	}

	auto rasterizerDescriptor = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	rasterizerDescriptor.CullMode = CullMap.at(state.mCullFace);
	// Preserve the engine winding convention in the top-left D3D viewport.
	rasterizerDescriptor.FrontCounterClockwise = state.mTriangleOrientation == TriangleOrientation::CW;

	const static std::unordered_map<RasterizationMode, D3D12_PRIMITIVE_TOPOLOGY_TYPE> TopologyTypeMap = {
		{RasterizationMode::POINT, D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT},
		{RasterizationMode::LINE, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE},
		{RasterizationMode::FILL, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE}
	};

	D3D12_PRIMITIVE_TOPOLOGY_TYPE topology = TopologyTypeMap.at(state.mRasterization);

	std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements;

	//TODO: move it (same map already in texturedx12)
	static const std::unordered_map<PixelFormat, DXGI_FORMAT> FormatMap = {
		{PixelFormat::R_FLOAT, DXGI_FORMAT_R32_FLOAT},
		{PixelFormat::RG_FLOAT, DXGI_FORMAT_R32G32_FLOAT},
		{PixelFormat::RGB_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT},
		{PixelFormat::RGBA_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT},
		{PixelFormat::R_INT, DXGI_FORMAT_R8_UNORM},
		{PixelFormat::RG_INT, DXGI_FORMAT_R8G8_UNORM},
		{PixelFormat::RGB_INT, DXGI_FORMAT_R8G8B8A8_UNORM},
		{PixelFormat::RGBA_INT, DXGI_FORMAT_R8G8B8A8_UNORM},
		{PixelFormat::R32_INT, DXGI_FORMAT_R32_SINT},
		{PixelFormat::RG32_INT, DXGI_FORMAT_R32G32_SINT},
		{PixelFormat::RGB32_INT, DXGI_FORMAT_R32G32B32_SINT},
		{PixelFormat::RGBA32_INT, DXGI_FORMAT_R32G32B32A32_SINT},
		{PixelFormat::BGRA_INT, DXGI_FORMAT_B8G8R8A8_UNORM},
		{PixelFormat::DEPTH_24_UNORM_STENCIL_8_UINT, DXGI_FORMAT_D24_UNORM_S8_UINT},
		{PixelFormat::DEPTH32_FLOAT, DXGI_FORMAT_D32_FLOAT},
		{PixelFormat::DEPTH32_FLOAT_S8X24_UINT, DXGI_FORMAT_D32_FLOAT_S8X24_UINT},
		{PixelFormat::DEPTH_32_FLOAT_STENCIL_8_UINT, DXGI_FORMAT_D32_FLOAT_S8X24_UINT},
		{ PixelFormat::RGBA16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT },
		{ PixelFormat::RG16_FLOAT, DXGI_FORMAT_R16G16_FLOAT },
		{ PixelFormat::R16_FLOAT, DXGI_FORMAT_R16_FLOAT },
		{ PixelFormat::R11G11B10_FLOAT, DXGI_FORMAT_R11G11B10_FLOAT },
	};
    validateVertexInputs(*state.mShader);
    if (mVertexLayout) {
        for (const auto& attribute : mVertexLayout->attributes) {
            const auto& stream=mVertexLayout->stream(attribute.binding);
            const bool instance=stream.rate==VertexInputRate::PerInstance;
            input_elements.push_back({"TEXCOORD",attribute.location,NativeVertexFormat(attribute.format),attribute.binding,attribute.offset,
                instance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                instance ? stream.stepRate : 0});
        }
    } else
	for (size_t i = 0; i < state.mShader->getReflection().mInputParams.size(); i++) {
		const auto& input = state.mShader->getReflection().mInputParams.at(i);
		if (input.mSize == 0 || input.mName.rfind("gl_", 0) == 0 || input.mLocation > 15) {
			continue;
		}
		const auto format = FormatMap.find(input.mFormat);
		if (format == FormatMap.end()) {
			continue;
		}

		input_elements.push_back(D3D12_INPUT_ELEMENT_DESC{
			.SemanticName = "TEXCOORD",
			.SemanticIndex = (UINT)input.mLocation,
			.Format = format->second,
			.InputSlot = 0,
			.AlignedByteOffset = (UINT)input.mOffset,
			.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, // TODO: InputRateMap.at(input_layout.rate),
			.InstanceDataStepRate = 0 // TODO: (UINT)(input_layout.rate == InputLayout::Rate::Vertex ? 0 : 1)
		});
	}

	std::shared_ptr<FrameBufferDx12> fb = mCurrentState.mFrameBuffer ? mCurrentState.mFrameBuffer : getDefaultFrameBuffer();

	auto shaderBlob = [&](ShaderType type) -> ID3DBlob* {
		const auto it = state.mShader->mBlobs.find(type);
		if (it == state.mShader->mBlobs.end()) {
			return nullptr;
		}
		return it->second.Get();
	};

	if (ID3DBlob* compute = shaderBlob(ShaderType::COMPUTE); compute && !shaderBlob(ShaderType::VERTEX) && !shaderBlob(ShaderType::FRAGMENT)) {
		D3D12_COMPUTE_PIPELINE_STATE_DESC computeDescriptor = {};
		computeDescriptor.pRootSignature = state.mShader->getRootSignature().Get();
		computeDescriptor.CS = CD3DX12_SHADER_BYTECODE(compute);
		computeDescriptor.NodeMask = 1;
		computeDescriptor.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> computeState;
		ThrowIfFailed(mDevice->CreateComputePipelineState(&computeDescriptor, IID_PPV_ARGS(&computeState)));
		TrackGpuObjectDx12(computeState.Get(), mDevice.Get(), PROFILER::GpuResourceKind::Pipeline, "Compute pipeline");
		return computeState;
	}

	const bool tessellation = shaderBlob(ShaderType::TESSELLATION_CONTROL) || shaderBlob(ShaderType::TESSELLATION_EVALUATION);
	if (tessellation) {
		topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDescriptor = {};
	if (ID3DBlob* vs = shaderBlob(ShaderType::VERTEX)) {
		psoDescriptor.VS = CD3DX12_SHADER_BYTECODE(vs);
	}
	if (ID3DBlob* ps = shaderBlob(ShaderType::FRAGMENT)) {
		psoDescriptor.PS = CD3DX12_SHADER_BYTECODE(ps);
	}
	if (ID3DBlob* gs = shaderBlob(ShaderType::GEOMETRY)) {
		psoDescriptor.GS = CD3DX12_SHADER_BYTECODE(gs);
	}
	if (ID3DBlob* hs = shaderBlob(ShaderType::TESSELLATION_CONTROL)) {
		psoDescriptor.HS = CD3DX12_SHADER_BYTECODE(hs);
	}
	if (ID3DBlob* ds = shaderBlob(ShaderType::TESSELLATION_EVALUATION)) {
		psoDescriptor.DS = CD3DX12_SHADER_BYTECODE(ds);
	}
	psoDescriptor.InputLayout = {input_elements.data(), (UINT)input_elements.size()};
	psoDescriptor.NodeMask = 1;
	psoDescriptor.PrimitiveTopologyType = topology;
	psoDescriptor.pRootSignature = state.mShader->getRootSignature().Get();
	psoDescriptor.SampleMask = UINT_MAX;
	auto& textures = fb->getTextures();
	psoDescriptor.NumRenderTargets = textures.size();
	int i = 0;
	for (auto texture : textures) {
		psoDescriptor.RTVFormats[i] = FormatMap.at(texture->getFormat());
		++i;
	}
	psoDescriptor.DSVFormat = FormatMap.at(fb->getDepth()->getFormat());
	psoDescriptor.SampleDesc.Count = 1;
	psoDescriptor.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	psoDescriptor.RasterizerState = rasterizerDescriptor;
	psoDescriptor.DepthStencilState = depthDescriptor;
	psoDescriptor.BlendState = blendDescriptor;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> result;
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
	mDevice.As(&infoQueue);
	if (infoQueue) {
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);
	}
	const HRESULT psoHr = mDevice->CreateGraphicsPipelineState(&psoDescriptor, IID_PPV_ARGS(&result));
	if (FAILED(psoHr)) {
		std::string message = "CreateGraphicsPipelineState failed";
		if (infoQueue) {
			const UINT64 count = infoQueue->GetNumStoredMessages();
			for (UINT64 i = 0; i < count; ++i) {
				SIZE_T length = 0;
				if (FAILED(infoQueue->GetMessage(i, nullptr, &length))) {
					continue;
				}
				std::vector<std::byte> storage(length);
				auto* msg = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
				if (SUCCEEDED(infoQueue->GetMessage(i, msg, &length)) && msg->pDescription) {
					message += "\n";
					message += msg->pDescription;
				}
			}
			infoQueue->ClearStoredMessages();
		}
		LOG_ERROR << message;
		throw std::runtime_error(message);
	}
	if (infoQueue) {
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
	}

	TrackGpuObjectDx12(result.Get(), mDevice.Get(), PROFILER::GpuResourceKind::Pipeline, "Graphics pipeline");
	return result;
}

void DriverDx12::applyState() {
	auto shader = mCurrentState.mShader;
	assert(shader);

	if (!mStates.contains((mCurrentState.getId() + ":" + std::to_string(shader->generation()) + ":" + vertexLayoutKey()))) {
		auto pipeline_state = createNewState(mCurrentState);
		mStates[(mCurrentState.getId() + ":" + std::to_string(shader->generation()) + ":" + vertexLayoutKey())] = pipeline_state;
	}

	const bool isCompute = shader->mBlobs.contains(ShaderType::COMPUTE) && shader->mBlobs.at(ShaderType::COMPUTE) &&
		!shader->mBlobs.contains(ShaderType::VERTEX);

	std::shared_ptr<FrameBufferDx12> fb;

	if (!mCurrentState.mFrameBuffer) {
		//default fb
		fb = getDefaultFrameBuffer();
	} else {
		fb = mCurrentState.mFrameBuffer;
	}
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtv_descriptors;
	for (auto& texture : fb->getTextures()) {
		std::static_pointer_cast<TextureDx12>(texture)->setState(mCommandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE heapStart;
	heapStart = fb->getTexHeap()->GetCPUDescriptorHandleForHeapStart();
	auto incSize = d3dUtil::GetDriver()->getDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	for (auto& texture : fb->getTextures()) {
		rtv_descriptors.push_back(heapStart);
		heapStart.Offset(1, incSize);
	}

	auto dsv_descriptor = fb->getDepthHeap()->GetCPUDescriptorHandleForHeapStart();

	if (!isCompute) {
		std::static_pointer_cast<TextureDx12>(fb->getDepth())->setState(mCommandList.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE);
		mCommandList->OMSetRenderTargets((UINT)rtv_descriptors.size(), rtv_descriptors.data(),
			FALSE, &dsv_descriptor);
	}

	const auto state = mStates.at((mCurrentState.getId() + ":" + std::to_string(shader->generation()) + ":" + vertexLayoutKey())).Get();
	mCommandList->SetPipelineState(state);
	if (isCompute) {
		mCommandList->SetComputeRootSignature(shader->getRootSignature().Get());
	} else {
		mCommandList->SetGraphicsRootSignature(shader->getRootSignature().Get());
	}
    if (shader->bindingMode()==ShaderBindingMode::Bindless) {
        ID3D12DescriptorHeap* heaps[]={mDescriptorHeap.Get(),mBindlessSamplerHeap.Get()};
        mCommandList->SetDescriptorHeaps(2,heaps);
    } else mCommandList->SetDescriptorHeaps(1, mDescriptorHeap.GetAddressOf());

	for (const auto& uniform : shader->getReflection().mUniforms) {
		const auto rootId = static_cast<UINT>(uniform.mRootId);
		const auto binding = uniform.mBind;
        if (shader->isBindlessResource(uniform)) {
            const auto snapshot=bindlessSnapshot(uniform,shader->descriptorCount(uniform));
            if (isCompute) mCommandList->SetComputeRootDescriptorTable(rootId,snapshot.first);
            else mCommandList->SetGraphicsRootDescriptorTable(rootId,snapshot.first);
            if (uniform.mSamplerRootId!=UINT32_MAX) {
                if (isCompute) mCommandList->SetComputeRootDescriptorTable(uniform.mSamplerRootId,snapshot.second);
                else mCommandList->SetGraphicsRootDescriptorTable(uniform.mSamplerRootId,snapshot.second);
            }
            continue;
        }
		
		if (uniform.mType == ShaderReflection::UniformType::SAMPLER_2D ||
			uniform.mType == ShaderReflection::UniformType::SAMPLER_CUBE || 
			uniform.mType == ShaderReflection::UniformType::SAMPLER_3D || 
			uniform.mType == ShaderReflection::UniformType::SAMPLER_2D_ARRAY) {
			const auto& texture = mTextures.at(binding);
			texture->setState(mCommandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			if (isCompute) {
				mCommandList->SetComputeRootDescriptorTable(rootId, texture->getGpuDescriptorHandle());
			} else {
				mCommandList->SetGraphicsRootDescriptorTable(rootId, texture->getGpuDescriptorHandle());
			}
		}
		else if (uniform.mType == ShaderReflection::UniformType::IMAGE_3D || uniform.mType == ShaderReflection::UniformType::IMAGE_2D) {
            auto texture=mTextures.at(binding);
            texture->setState(mCommandList.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			const auto storageImage = mStorageImageBindings.find(binding);
			const D3D12_GPU_DESCRIPTOR_HANDLE uav = storageImage != mStorageImageBindings.end()
				? storageImageUav(storageImage->second)
				: D3D12_GPU_DESCRIPTOR_HANDLE(texture->getUavHandle());
			mCommandList->SetComputeRootDescriptorTable(rootId, uav);
        }
		else if (uniform.mType == ShaderReflection::UniformType::UNIFORM_BUFFER) {
			auto ubo = mUniformBuffers.at(binding);
			const auto address = ubo->getGpuAddress();
			if (isCompute) {
				mCommandList->SetComputeRootConstantBufferView(rootId, address);
			} else {
				mCommandList->SetGraphicsRootConstantBufferView(rootId, address);
			}
		}
		else if (uniform.mType == ShaderReflection::UniformType::STORAGE_BUFFER) {
			auto ssbo = mStorageBuffers.at(binding);
			if (shader->bindingMode()==ShaderBindingMode::Bindless && uniform.mReadOnly) {
				BindlessResourceTable local;local.capacity=1;local.entries.push_back({{},ssbo});
				const auto snapshot=bindlessSnapshot(uniform,1,&local);
				if (isCompute) mCommandList->SetComputeRootDescriptorTable(rootId,snapshot.first);
				else mCommandList->SetGraphicsRootDescriptorTable(rootId,snapshot.first);
				continue;
			}
			const auto& ssboResource = ssbo->getBuffer();
			if (!ssboResource) {
				continue;
			}
			ssbo->transition(mCommandList.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			const auto uav = ssbo->getUAVHandler();
			if (uav.ptr == 0) {
				continue;
			}
			if (isCompute) {
				mCommandList->SetComputeRootDescriptorTable(rootId, uav);
			} else {
				mCommandList->SetGraphicsRootDescriptorTable(rootId, uav);
			}
		}
		else if (uniform.mType == ShaderReflection::UniformType::PUSH_CONSTANT) {
			continue;
		}
		else {
			assert(false);
		}
	}

	flushPushConstants(isCompute);
    if(isCompute) return;

	if (shader->mBlobs.contains(ShaderType::TESSELLATION_CONTROL) && shader->mBlobs.at(ShaderType::TESSELLATION_CONTROL)) {
		mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
		clearDirty(Dirty::PRIMITIVE_MODE);
	} else if (isDirty(Dirty::PRIMITIVE_MODE)) {
		clearDirty(Dirty::PRIMITIVE_MODE);
		mCommandList->IASetPrimitiveTopology(TopologyMap.at(mPrimitiveMode));
	}
	if (isDirty(Dirty::VIEWPORT)) {
		clearDirty(Dirty::VIEWPORT);
		const auto viewport = mViewport.value_or(Viewport{{0.0f, 0.0f}, {static_cast<float>(mWidth), static_cast<float>(mHeight)}});
		D3D12_VIEWPORT vp{
			.TopLeftX = viewport.mPosition.x,
			.TopLeftY = viewport.mPosition.y,
			.Width = viewport.mSize.x,
			.Height = viewport.mSize.y,
			.MinDepth = viewport.mMinDepth,
			.MaxDepth = viewport.mMaxDepth,
		};
		mCommandList->RSSetViewports(1, &vp);
	}
	if (isDirty(Dirty::SCISSOR)) {
		clearDirty(Dirty::SCISSOR);
		const auto scissor = mScissor.value_or(Scissor{{0.0f, 0.0f}, {static_cast<float>(fb->getWidth()), static_cast<float>(fb->getHeight())}});
		D3D12_RECT rect {
			.left = static_cast<LONG>(scissor.mPosition.x),
			.top = static_cast<LONG>(scissor.mPosition.y),
			.right = static_cast<LONG>(scissor.mPosition.x + scissor.mSize.x),
			.bottom = static_cast<LONG>(scissor.mPosition.y + scissor.mSize.y)
		};
		mCommandList->RSSetScissorRects(1, &rect);
	}
	if (isDirty(Dirty::VERTEX_BUFFER)) {
		clearDirty(Dirty::VERTEX_BUFFER);
        if (mVertexLayout) {
            for (const auto& [binding,buffer] : mVertexStreams) {
                const auto native=std::static_pointer_cast<VertexBufferDx12>(buffer);
                auto view=D3D12_VERTEX_BUFFER_VIEW{native->getBuffer()->GetGPUVirtualAddress(),static_cast<UINT>(buffer->getSizeByte()),static_cast<UINT>(buffer->getStride())};
                mCommandList->IASetVertexBuffers(binding,1,&view);
            }
        } else if (mVertexBuffer) {
		auto view = D3D12_VERTEX_BUFFER_VIEW{
			.BufferLocation = mVertexBuffer->getBuffer()->GetGPUVirtualAddress(),
			.SizeInBytes = static_cast<unsigned>(mVertexBuffer->getSizeByte()),
			.StrideInBytes = static_cast<unsigned>(mVertexBuffer->getStride()),
		};
		mCommandList->IASetVertexBuffers(0, 1, &view);
        }

	}
	if (mIndexBuffer && isDirty(Dirty::INDEX_BUFFER)) {
		clearDirty(Dirty::INDEX_BUFFER);
		D3D12_INDEX_BUFFER_VIEW bufferView{
			.BufferLocation = mIndexBuffer->getBuffer()->GetGPUVirtualAddress(),
			.SizeInBytes = static_cast<unsigned>(mIndexBuffer->getSizeByte()),
			.Format = mIndexBuffer->getStride() == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT
		};
		mCommandList->IASetIndexBuffer(&bufferView);
	}
}

std::shared_ptr<ShaderInterface> DriverDx12::createShader(const std::string& vertexPath, const std::string& fragmentPath) {
	std::map<ShaderType, std::string> paths;
	paths[ShaderType::VERTEX] = vertexPath;
	paths[ShaderType::FRAGMENT] = fragmentPath;
	return ShaderDx12::CreateFromPath(std::move(paths));
}

std::shared_ptr<ShaderInterface> DriverDx12::createShader(const ShaderResource& res, UTILS::IAllocator* allocator, ShaderDeleter deleter) {
	return AllocateShader<ShaderDx12>(allocator, std::move(deleter), res);
}

std::shared_ptr<ModelInterface> DriverDx12::createModel(const std::string& path, UTILS::IAllocator* allocator, ModelDeleter deleter) {
	ModelDeleter finalDeleter = [deleter](ModelInterface* m) {
		if (deleter) deleter(m);
	};
	return AllocateModel<ModelDx12>(allocator, std::move(finalDeleter), path);
}

std::shared_ptr<MaterialInterface> DriverDx12::createMaterial(const MaterialResource& res, UTILS::IAllocator* allocator, MaterialDeleter deleter) {
	MaterialDeleter finalDeleter = [deleter](MaterialInterface* m) {
		if (deleter) deleter(m);
	};
	return AllocateMaterial<MaterialDx12>(allocator, std::move(finalDeleter), res);
}

std::shared_ptr<UniformBufferInterface> DriverDx12::createUniformBuffer(const void* data, size_t size) {
	return std::make_shared<UniformBufferDx12>(data, size);
}

std::shared_ptr<StorageBufferInterface> DriverDx12::createStorageBuffer(const void* data, size_t size, size_t stride) {
	return std::make_shared<StorageBufferDx12>(data, size, stride);
}

std::shared_ptr<FrameBufferInterface> DriverDx12::createFrameBuffer(const std::vector<std::shared_ptr<TextureInterface>>& textures, std::shared_ptr<TextureInterface> depth, unsigned mip, unsigned layer) {
	return std::make_shared<FrameBufferDx12>(textures, depth, mip, layer);
}

void DriverDx12::setFrameBuffer(std::shared_ptr<FrameBufferInterface> frameBuffer) {
	mCurrentState.mFrameBuffer = std::static_pointer_cast<FrameBufferDx12>(frameBuffer);
}

void DriverDx12::resetFrameBuffer() {
	mCurrentState.mFrameBuffer = nullptr;
}

IKIGAI::RENDER::BindlessCapabilities IKIGAI::RENDER::DriverDx12::bindlessCapabilities() const {
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    if (FAILED(mDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,&options,sizeof(options))) || options.ResourceBindingTier<D3D12_RESOURCE_BINDING_TIER_2) return {};
    return {true,true,false,true,options.ResourceBindingTier>=D3D12_RESOURCE_BINDING_TIER_3,128,64};
}

std::pair<D3D12_GPU_DESCRIPTOR_HANDLE,D3D12_GPU_DESCRIPTOR_HANDLE>
IKIGAI::RENDER::DriverDx12::bindlessSnapshot(const ShaderReflection::Uniform& uniform,uint32_t count,const BindlessResourceTable* localTable) {
    const auto& table=localTable?*localTable:bindlessTable(uniform,count);
    std::string key=std::to_string(uniform.mBind)+":"+std::to_string(table.revision)+":"+std::to_string(uniform.mReadOnly);
    const bool textures=bool(table.get(0).texture);
    for (uint32_t i=0;i<count;++i) {
        const auto entry=table.get(i);
        if (entry.texture) {
            auto texture=std::static_pointer_cast<TextureDx12>(entry.texture);
            texture->setState(mCommandList.Get(),D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
            key+=":"+std::to_string(texture->getRevision());
        } else {
            auto buffer=std::static_pointer_cast<StorageBufferDx12>(entry.buffer);
            key+=":"+std::to_string(buffer->getBuffer()->GetGPUVirtualAddress())+":"+std::to_string(buffer->getSizeByte());
            buffer->transition(mCommandList.Get(),uniform.mReadOnly?D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }
    if (const auto it=mBindlessSnapshots.find(key);it!=mBindlessSnapshots.end()) return it->second;
    if (count>1064+65536-mBindlessDescriptorCursor || (textures && count>2048-mBindlessSamplerCursor))
        throw std::runtime_error("Bindless descriptor snapshot capacity exhausted in current frame");
    auto cpu=mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(mBindlessDescriptorCursor)*mDescriptorIncSize;
    auto gpu=mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=UINT64(mBindlessDescriptorCursor)*mDescriptorIncSize;
    auto samplerCpu=mBindlessSamplerHeap->GetCPUDescriptorHandleForHeapStart();
    auto samplerGpu=mBindlessSamplerHeap->GetGPUDescriptorHandleForHeapStart();
    const auto samplerStride=mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    samplerCpu.ptr+=SIZE_T(mBindlessSamplerCursor)*samplerStride;samplerGpu.ptr+=UINT64(mBindlessSamplerCursor)*samplerStride;
    for (uint32_t i=0;i<count;++i) {
        const auto entry=table.get(i);mBindlessFrameResources.push_back(entry);
        if (entry.texture) {
            const auto t=std::static_pointer_cast<TextureDx12>(entry.texture);
            auto* resource=t->getResource().Get();
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Format=resource->GetDesc().Format;
            if (view.Format==DXGI_FORMAT_R32_TYPELESS) view.Format=DXGI_FORMAT_R32_FLOAT;
            if (view.Format==DXGI_FORMAT_R24G8_TYPELESS) view.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            switch (t->getType()) {
            case TextureType::TEXTURE_CUBE:view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURECUBE;view.TextureCube.MipLevels=t->getMipCount();break;
            case TextureType::TEXTURE_3D:view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D;view.Texture3D.MipLevels=t->getMipCount();break;
            case TextureType::TEXTURE_2D_ARRAY:view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;view.Texture2DArray.MipLevels=t->getMipCount();view.Texture2DArray.ArraySize=resource->GetDesc().DepthOrArraySize;break;
            default:view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=t->getMipCount();break;
            }
            mDevice->CreateShaderResourceView(resource,&view,cpu);
            auto wrap=[](WrapFilter value) {switch(value) {
                case WrapFilter::REPEAT:return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                case WrapFilter::MIRRORED_REPEAT:return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
                case WrapFilter::CLAMP_TO_BORDER:return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
                default:return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;}};
            D3D12_SAMPLER_DESC sampler{};
            const auto min=t->getMinFilter()==MinMagFilter::NEAREST?D3D12_FILTER_TYPE_POINT:D3D12_FILTER_TYPE_LINEAR;
            const auto mag=t->getMagFilter()==MinMagFilter::NEAREST?D3D12_FILTER_TYPE_POINT:D3D12_FILTER_TYPE_LINEAR;
            sampler.Filter=D3D12_ENCODE_BASIC_FILTER(min,mag,min,D3D12_FILTER_REDUCTION_TYPE_STANDARD);
            sampler.AddressU=wrap(t->getWrapS());sampler.AddressV=wrap(t->getWrapT());sampler.AddressW=wrap(t->getWrapR());
            sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_NEVER;sampler.MaxLOD=D3D12_FLOAT32_MAX;
            mDevice->CreateSampler(&sampler,samplerCpu);samplerCpu.ptr+=samplerStride;
        } else {
            const auto b=std::static_pointer_cast<StorageBufferDx12>(entry.buffer);
            if (b->getSizeByte()%4) throw std::runtime_error("Bindless byte-address buffer size must be a multiple of four");
            if (uniform.mReadOnly) {
                D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;view.Format=DXGI_FORMAT_R32_TYPELESS;
                view.Buffer.NumElements=static_cast<UINT>(b->getSizeByte()/4);view.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
                mDevice->CreateShaderResourceView(b->getBuffer().Get(),&view,cpu);
            } else {
                D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;view.Format=DXGI_FORMAT_R32_TYPELESS;
                view.Buffer.NumElements=static_cast<UINT>(b->getSizeByte()/4);view.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
                mDevice->CreateUnorderedAccessView(b->getBuffer().Get(),nullptr,&view,cpu);
            }
        }
        cpu.ptr+=mDescriptorIncSize;
    }
    mBindlessDescriptorCursor+=count;if (textures) mBindlessSamplerCursor+=count;
    const auto result=std::make_pair(gpu,samplerGpu);mBindlessSnapshots.emplace(key,result);return result;
}

void IKIGAI::RENDER::DriverDx12::draw(const DrawArguments& args) {
    if (!args.count || !args.instanceCount) return;
    validateVertexDraw(args);
    applyState();
    if (args.indexed) mCommandList->DrawIndexedInstanced(args.count,args.instanceCount,args.first,args.baseVertex,args.firstInstance);
    else mCommandList->DrawInstanced(args.count,args.instanceCount,args.first,args.firstInstance);
}

namespace {
const bool kRegisterDx12Backend = [] {
	IKIGAI::RENDER::RegisterRenderBackend(IKIGAI::RENDER::RenderSettings::Backend::DIRECTX12,
		[] { return std::make_unique<IKIGAI::RENDER::DriverDx12>(); });
	return true;
}();
}
#endif
