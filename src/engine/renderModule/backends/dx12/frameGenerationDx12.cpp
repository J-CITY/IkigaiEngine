#include "driverDx12.h"
#ifdef DX12_BACKEND
#include "textureDx12.h"
#include <utilsModule/imguiHelper/imguiBackend/imguiBackend.h>
#ifdef IKIGAI_USE_STREAMLINE
#include "../streamlineRuntime.h"
#endif
#ifdef IKIGAI_USE_FSR_FRAME_GENERATION
#include "../upscalerSdkUtils.h"
#include <algorithm>
#include <dx12/ffx_api_framegeneration_dx12.h>
#include <ffx_framegeneration.h>
#include <iostream>
#include <vector>

namespace IKIGAI::RENDER {
class FsrFrameGenerationDx12 final : public FrameGenerationInterface {
	DriverDx12& d;
	UpscalerModule module { L"amd_fidelityfx_loader_dx12.dll" };
	PfnFfxCreateContext create = module.function<PfnFfxCreateContext>("ffxCreateContext");
	PfnFfxDestroyContext destroy = module.function<PfnFfxDestroyContext>("ffxDestroyContext");
	PfnFfxConfigure configureApi = module.function<PfnFfxConfigure>("ffxConfigure");
	PfnFfxDispatch dispatch = module.function<PfnFfxDispatch>("ffxDispatch");
	PfnFfxQuery query = module.function<PfnFfxQuery>("ffxQuery");
	ffxContext fg {};
	ffxContext swap {};
	FrameGenerationStatus state {};
	MATH::Vector2i renderSize {};
	MATH::Vector2i displaySize {};
	struct PendingInput {
		FrameGenerationInputs inputs;
		bool presented = false;
	};
	std::vector<PendingInput> pendingInputs;
	uint64_t observedPresents = 0;
	uint64_t pendingRealPresents = 0;
	bool attached = false;
	bool prepared = false;
	static void check(ffxReturnCode_t code, const char* call) {
		if (code) {
			throw UnsupportedFrameGeneration(std::string(call) + ": " + std::to_string(code));
		}
	}
	void attach() {
		d.end();
		d.wait();
		auto& ctx = d.mSwapchains.at(d.mCurrentWindowID);
		for (auto& f : ctx.mDefaultFb) {
			f.reset();
		}
		d.mCurrentState.mFrameBuffer.reset();
		d.clearForDestroy();
		Microsoft::WRL::ComPtr<IDXGISwapChain4> native;
		check(ctx.mSwapChain.As(&native), "Query DXGI swapchain4");
		auto* wrapped = native.Detach();
		// FFX consumes the original reference and creates another HWND swapchain.
		// DXGI permits only one flip-model swapchain per HWND.
		ctx.mSwapChain.Reset();
		ffxCreateContextDescFrameGenerationSwapChainVersionDX12 version {};
		version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_DX12;
		version.version = FFX_FRAMEGENERATION_SWAPCHAIN_DX12_VERSION;
		ffxCreateContextDescFrameGenerationSwapChainWrapDX12 desc {};
		desc.header = { FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WRAP_DX12, &version.header };
		desc.swapchain = &wrapped;
		desc.gameQueue = d.mCommandQueue.Get();
		const auto code = create(&swap, &desc.header, nullptr);
		if (!code) {
			wrapped->QueryInterface(IID_PPV_ARGS(ctx.mSwapChain.GetAddressOf()));
			wrapped->Release();
			attached = true;
			observedPresents = 0;
		}
		else {
			d.createSwapChain(d.mCurrentWindowID, ctx.width, ctx.height);
		}
		d.mFrameId = ctx.mSwapChain->GetCurrentBackBufferIndex();
		d.createDefaultFrameBuffer(d.mCurrentWindowID, ctx.width, ctx.height);
		d.begin();
		check(code, "FSR FG wrap DX12 swapchain");
	}
	void releaseFG() {
		if (fg) {
			destroy(&fg, nullptr);
			fg = nullptr;
		}
		prepared = false;
	}

public:
	explicit FsrFrameGenerationDx12(DriverDx12& driver)
		: d(driver) {
		state.Technology = FrameGenerationType::FSR;
		state.MaxMultiplier = 2;
		state.Multiplier = 2;
	}
	~FsrFrameGenerationDx12() override {
		try {
			if (attached && !d.mShuttingDown && !d.hasGpuFailure()) {
				d.end();
				d.wait();
			}
			if (fg && !d.isDeviceLost()) {
				suspend();
			}
			if (swap && !d.isDeviceLost()) {
				waitForPresents();
			}
			releaseFG();
			if (swap) {
				destroy(&swap, nullptr);
				swap = nullptr;
			}
			if (attached) {
				auto& ctx = d.mSwapchains.at(d.mCurrentWindowID);
				for (auto& f : ctx.mDefaultFb) {
					f.reset();
				}
				d.mCurrentState.mFrameBuffer.reset();
				d.clearForDestroy();
				ctx.mSwapChain.Reset();
				if (!d.mShuttingDown && !d.hasGpuFailure()) {
					d.createSwapChain(d.mCurrentWindowID, ctx.width, ctx.height);
					d.mFrameId = ctx.mSwapChain->GetCurrentBackBufferIndex();
					d.createDefaultFrameBuffer(d.mCurrentWindowID, ctx.width, ctx.height);
					d.begin();
				}
			}
		} catch (const std::exception& error) {
			std::cerr << "FSR FG teardown: " << error.what() << '\n';
		}
	}
	void configure(const FrameGenerationSettings& settings, MATH::Vector2i render, MATH::Vector2i display) override {
		if (settings.multiplier != 2) {
			throw UnsupportedFrameGeneration("FSR FG supports x2 in this SDK");
		}
		if (fg && render == renderSize && display == displaySize) {
			return;
		}
		if (fg) {
			suspend();
			waitForPresents();
			releaseFG();
		}
		ffxCreateBackendDX12Desc backend {};
		backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
		backend.device = d.getDevice().Get();
		ffxCreateContextDescFrameGenerationVersion version {};
		version.header = { FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION, &backend.header };
		version.version = FFX_FRAMEGENERATION_VERSION;
		ffxCreateContextDescFrameGeneration desc {};
		desc.header = { FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION, &version.header };
		desc.displaySize = { uint32_t(display.x), uint32_t(display.y) };
		desc.maxRenderSize = { uint32_t(render.x), uint32_t(render.y) };
		desc.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
		check(create(&fg, &desc.header, nullptr), "FSR FG create DX12 context");
		if (!attached) {
			attach();
		}
		ffxQueryGetProviderVersion v {};
		v.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
		if (!query(&fg, &v.header) && v.versionName) {
			state.Implementation = v.versionName;
		}
		renderSize = render;
		displaySize = display;
		state.Supported = true;
		std::cout << "FRAMEGEN_CONTEXT FSR " << state.Implementation << " " << display.x << "x" << display.y << '\n';
	}
	FfxApiResource resource(const std::shared_ptr<TextureInterface>& texture) {
		auto t = std::dynamic_pointer_cast<TextureDx12>(texture);
		if (!t) {
			throw UnsupportedFrameGeneration("FSR FG requires DX12 textures");
		}
		t->setState(d.mCommandList.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		auto result = ffxApiGetResourceDX12(t->getResource().Get(), FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		if (texture->getType() == TextureType::DEPTH) {
			result.description.usage |= FFX_API_RESOURCE_USAGE_DEPTHTARGET;
		}
		return result;
	}
	void prepare(const FrameGenerationInputs& f) override {
		if (pendingInputs.empty()) {
			UINT count = 0;
			if (SUCCEEDED(d.mSwapchains.at(d.mCurrentWindowID).mSwapChain->GetLastPresentCount(&count))) {
				observedPresents = count;
			}
		}
		// The interpolation command list and swapchain presentation may outlive
		// the game command list. Keep every submitted input alive until the SDK
		// confirms all pending presents have completed.
		pendingInputs.push_back({ f, false });
		ffxConfigureDescFrameGeneration config {};
		config.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
		config.swapChain = d.mSwapchains.at(d.mCurrentWindowID).mSwapChain.Get();
		config.frameGenerationEnabled = true;
		config.allowAsyncWorkloads = false;
		config.frameID = f.RealFrameId;
		config.generationRect = { 0, 0, f.DisplaySize.x, f.DisplaySize.y };
		check(configureApi(&fg, &config.header), "FSR FG configure");
		ffxConfigureDescFrameGenerationSwapChainRegisterUiResourceDX12 ui {};
		ui.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_REGISTERUIRESOURCE_DX12;
		ui.uiResource = resource(f.UI);
		ui.flags
			= FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_USE_PREMUL_ALPHA | FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_ENABLE_INTERNAL_UI_DOUBLE_BUFFERING;
		check(configureApi(&swap, &ui.header), "FSR FG register UI");
		ffxDispatchDescFrameGenerationPrepareV2 prep {};
		prep.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
		prep.frameID = f.RealFrameId;
		prep.commandList = d.mCommandList.Get();
		prep.renderSize = { uint32_t(f.RenderSize.x), uint32_t(f.RenderSize.y) };
		prep.jitterOffset = { f.Jitter.x, f.Jitter.y };
		prep.motionVectorScale = { -float(f.RenderSize.x), -float(f.RenderSize.y) };
		prep.frameTimeDelta = f.DeltaMilliseconds;
		prep.reset = f.Reset;
		prep.cameraNear = f.Near;
		prep.cameraFar = f.Far;
		prep.cameraFovAngleVertical = f.VerticalFov;
		prep.viewSpaceToMetersFactor = 1;
		prep.depth = resource(f.Depth);
		prep.motionVectors = resource(f.MotionVectors);
		for (unsigned i = 0; i < 3; ++i) {
			prep.cameraPosition[i] = f.Position[i];
			prep.cameraUp[i] = f.Up[i];
			prep.cameraRight[i] = f.Right[i];
			prep.cameraForward[i] = f.Forward[i];
		}
		check(dispatch(&fg, &prep.header), "FSR FG prepare");
		void* commands = nullptr;
		FfxApiResource output {};
		ffxQueryDescFrameGenerationSwapChainInterpolationCommandListDX12 cmd {};
		cmd.header.type = FFX_API_QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_INTERPOLATIONCOMMANDLIST_DX12;
		cmd.pOutCommandList = &commands;
		check(query(&swap, &cmd.header), "FSR FG interpolation commands");
		ffxQueryDescFrameGenerationSwapChainInterpolationTextureDX12 tex {};
		tex.header.type = FFX_API_QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_INTERPOLATIONTEXTURE_DX12;
		tex.pOutTexture = &output;
		check(query(&swap, &tex.header), "FSR FG interpolation output");
		ffxDispatchDescFrameGeneration gen {};
		gen.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
		gen.commandList = commands;
		gen.presentColor = resource(f.Color);
		gen.outputs[0] = output;
		gen.numGeneratedFrames = 1;
		gen.reset = f.Reset;
		gen.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
		gen.minMaxLuminance[0] = 0;
		gen.minMaxLuminance[1] = 1;
		gen.frameID = f.RealFrameId;
		gen.generationRect = config.generationRect;
		check(dispatch(&fg, &gen.header), "FSR FG generate");
		d.invalidateUpscalerState();
		state.Active = true;
		prepared = true;
		if (state.RealFrames == 0 || f.Reset) {
			std::cout << "FRAMEGEN_PREPARE FSR frame=" << f.RealFrameId << " reset=" << f.Reset << '\n';
		}
	}
	void suspend() override {
		if (fg) {
			ffxConfigureDescFrameGeneration config {};
			config.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
			config.swapChain = d.mSwapchains.at(d.mCurrentWindowID).mSwapChain.Get();
			configureApi(&fg, &config.header);
		}
		if (swap) {
			ffxConfigureDescFrameGenerationSwapChainRegisterUiResourceDX12 ui {};
			ui.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_REGISTERUIRESOURCE_DX12;
			configureApi(&swap, &ui.header);
		}
		if (pendingRealPresents) {
			waitForPresents();
		}
		prepared = false;
		state.Active = false;
	}
	void waitForFrame(uint64_t realFrameId) override {
		if (std::any_of(pendingInputs.begin(), pendingInputs.end(),
				[realFrameId](const PendingInput& pending) { return pending.inputs.RealFrameId == realFrameId && pending.presented; })) {
			waitForPresents();
		}
	}
	void waitForPresents() override {
		if (swap) {
			ffxDispatchDescFrameGenerationSwapChainWaitForPresentsDX12 wait {};
			wait.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12;
			DriverDx12::watchGpuWait(
				"DX12 FSR frame generation wait for presents timed out", [&] { check(dispatch(&swap, &wait.header), "FSR FG wait for presents"); });
			UINT count = 0;
			if (SUCCEEDED(d.mSwapchains.at(d.mCurrentWindowID).mSwapChain->GetLastPresentCount(&count))) {
				const auto batch = count >= observedPresents ? count - observedPresents : count;
				observedPresents = count;
				if (pendingRealPresents) {
					state.PresentedFrames += batch;
					state.GeneratedFrames += batch > pendingRealPresents ? batch - pendingRealPresents : 0;
				}
			}
			if (pendingRealPresents) {
				std::cout << "FRAMEGEN_COMPLETE FSR real=" << state.RealFrames << " generated=" << state.GeneratedFrames
						  << " presented=" << state.PresentedFrames << '\n';
			}
			pendingRealPresents = 0;
			std::erase_if(pendingInputs, [](const PendingInput& pending) { return pending.presented; });
		}
	}
	void afterPresent() override {
		if (prepared) {
			++state.RealFrames;
			++pendingRealPresents;
			pendingInputs.back().presented = true;
			std::cout << "FRAMEGEN_PRESENT FSR real=" << state.RealFrames << " generated=" << state.GeneratedFrames
					  << " presented=" << state.PresentedFrames << '\n';
		}
		prepared = false;
	}
	FrameGenerationStatus status() const override {
		return state;
	}
	bool composesUI() const override {
		return attached;
	}
};
}
#endif
std::unique_ptr<IKIGAI::RENDER::FrameGenerationInterface> IKIGAI::RENDER::DriverDx12::createFrameGeneration(FrameGenerationType type) {
#ifdef IKIGAI_USE_DLSS_FRAME_GENERATION
	if (type == FrameGenerationType::DLSS) {
		return CreateStreamlineFrameGeneration(std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime), streamlineCallbacks());
	}
#endif
#ifdef IKIGAI_USE_FSR_FRAME_GENERATION
	if (type == FrameGenerationType::FSR) {
		try {
			return std::make_unique<FsrFrameGenerationDx12>(*this);
		} catch (const UnsupportedUpscaler& e) {
			throw UnsupportedFrameGeneration(e.what());
		}
	}
#endif
	throw UnsupportedFrameGeneration("Requested Frame Generation SDK is not compiled for DX12");
}
void IKIGAI::RENDER::DriverDx12::renderPresentationUI(std::shared_ptr<FrameBufferInterface> target) {
	for (auto& texture : mImGuiTextures) {
		texture->setState(mCommandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		mRayFrameResources.push_back(std::move(texture));
	}
	mImGuiTextures.clear();
	mCommandList->SetDescriptorHeaps(1, mDescriptorHeap.GetAddressOf());
	if (auto* imgui = IKIGAI::IMGUI::Get()) {
		imgui->renderDrawDataTo(std::move(target));
	}
	mPresentationUIRendered = true;
	invalidateUpscalerState();
}
#endif
