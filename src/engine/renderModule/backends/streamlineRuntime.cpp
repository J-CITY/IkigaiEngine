#include "streamlineRuntime.h"
#ifdef IKIGAI_USE_STREAMLINE
#include "interface/driverInterface.h"
#include <deque>
#include <iostream>
#include <mutex>
#include <sl_security.h>

#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")

namespace IKIGAI::RENDER {
namespace {
	std::weak_ptr<StreamlineRuntime> sharedRuntime;
	std::atomic<int64_t> apiError { 0 };
	void sdkLog(sl::LogType, const char* text) {
		std::cerr << "STREAMLINE " << text << '\n';
	}
	void sdkError(const sl::APIError& error) {
		apiError.store(error.hres ? error.hres : error.vkRes, std::memory_order_relaxed);
	}
	sl::float4x4 sdkMatrix(MATH::Matrix4f m) {
		sl::float4x4 result {};
		// Engine uses column vectors; Streamline uses row-vector matrices.
		for (unsigned r = 0; r < 4; ++r) {
			result[r] = { m(0, r), m(1, r), m(2, r), m(3, r) };
		}
		return result;
	}
	sl::DLSSMode sdkQuality(UpscalerQuality q) {
		switch (q) {
		case UpscalerQuality::QUALITY:
			return sl::DLSSMode::eMaxQuality;
		case UpscalerQuality::BALANCED:
			return sl::DLSSMode::eBalanced;
		default:
			return sl::DLSSMode::eMaxPerformance;
		}
	}
}

StreamlineRuntime::StreamlineRuntime(sl::RenderAPI api)
	: Api(api) {
	const auto path = UpscalerExecutableDirectory() / L"sl.interposer.dll";
	if (!sl::security::verifyEmbeddedSignature(path.c_str())) {
		throw UnsupportedFrameGeneration("Streamline interposer signature validation failed");
	}
	module = std::make_shared<UpscalerModule>(L"sl.interposer.dll");
	auto init = symbol<PFun_slInit*>("slInit");
	shutdown = symbol<PFun_slShutdown*>("slShutdown");
	supported = symbol<PFun_slIsFeatureSupported*>("slIsFeatureSupported");
	loadFeature = symbol<PFun_slSetFeatureLoaded*>("slSetFeatureLoaded");
	requirements = symbol<PFun_slGetFeatureRequirements*>("slGetFeatureRequirements");
	upgrade = symbol<PFun_slUpgradeInterface*>("slUpgradeInterface");
	nativeInterface = symbol<PFun_slGetNativeInterface*>("slGetNativeInterface");
	setD3DDevice = symbol<PFun_slSetD3DDevice*>("slSetD3DDevice");
	featureFunction = symbol<PFun_slGetFeatureFunction*>("slGetFeatureFunction");
	getToken = symbol<PFun_slGetNewFrameToken*>("slGetNewFrameToken");
	setConstants = symbol<PFun_slSetConstants*>("slSetConstants");
	setTags = symbol<PFun_slSetTagForFrame*>("slSetTagForFrame");
	evaluate = symbol<PFun_slEvaluateFeature*>("slEvaluateFeature");
	freeResources = symbol<PFun_slFreeResources*>("slFreeResources");
	std::vector<sl::Feature> features;
#ifdef IKIGAI_USE_DLSS
	features.push_back(sl::kFeatureDLSS);
#endif
#ifdef IKIGAI_USE_DLSS_FRAME_GENERATION
	features.insert(features.end(), { sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL });
#endif
	sl::Preferences prefs {};
	prefs.renderAPI = api;
	prefs.projectId = IKIGAI_DLSS_PROJECT_ID;
	prefs.engine = sl::EngineType::eCustom;
	prefs.engineVersion = "1.0";
	prefs.flags
		= sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
	prefs.featuresToLoad = features.data();
	prefs.numFeaturesToLoad = static_cast<uint32_t>(features.size());
	prefs.logMessageCallback = sdkLog;
	prefs.showConsole = false;
	check(init(prefs, sl::kSDKVersion), "slInit");
	Initialized = true;
	// Native/FSR presenters need common bookkeeping without attaching a second
	// FG swapchain wrapper. Use the pinned plugin's manual-hook gateway.
	commonModule = std::make_shared<UpscalerModule>(L"sl.common.dll");
	commonFunction = commonModule->function<void* (*)(const char*)>("slGetPluginFunction");
}
StreamlineRuntime::~StreamlineRuntime() {
	close();
}
void StreamlineRuntime::close() {
	if (Initialized) {
		shutdown();
		Initialized = false;
		Token = nullptr;
	}
}

std::shared_ptr<StreamlineRuntime> AcquireStreamline(sl::RenderAPI api) {
	if (auto existing = sharedRuntime.lock()) {
		if (existing->Api != api) {
			throw UnsupportedFrameGeneration("Streamline runtime is attached to another graphics API");
		}
		return existing;
	}
	auto result = std::make_shared<StreamlineRuntime>(api);
	sharedRuntime = result;
	return result;
}

void StreamlineRuntime::deviceReady(const sl::AdapterInfo& adapter) {
	auto sr = supported(sl::kFeatureDLSS, adapter);
	DLSSAvailable = sr == sl::Result::eOk;
	if (!DLSSAvailable) {
		SRReason = "Streamline DLSS SR unsupported: " + std::to_string(static_cast<int>(sr));
	}
	auto fg = supported(sl::kFeatureDLSS_G, adapter);
	FGAvailable = fg == sl::Result::eOk;
	if (!FGAvailable) {
		FGReason = "Streamline DLSS FG unsupported: " + std::to_string(static_cast<int>(fg));
	}
	ReflexAvailable = supported(sl::kFeatureReflex, adapter) == sl::Result::eOk;
	if (FGAvailable && !ReflexAvailable) {
		FGAvailable = false;
		FGReason = "DLSS FG requires Streamline Reflex";
	}
	if (ReflexAvailable) {
		sl::ReflexOptions options {};
		options.mode = sl::ReflexMode::eLowLatency;
		check(feature<PFun_slReflexSetOptions*>(sl::kFeatureReflex, "slReflexSetOptions")(options), "slReflexSetOptions");
	}
}

void StreamlineRuntime::beginFrame(uint32_t frame) {
	Frame = frame;
	CameraReady = false;
	check(getToken(Token, &frame), "slGetNewFrameToken");
	if (ReflexAvailable) {
		if (DriverInterface::settings.frameGeneration.technology == FrameGenerationType::DLSS
			|| DriverInterface::settings.frameGeneration.technology == FrameGenerationType::AUTO) {
			check(feature<PFun_slReflexSleep*>(sl::kFeatureReflex, "slReflexSleep")(*Token), "slReflexSleep");
		}
		marker(sl::PCLMarker::eSimulationStart);
	}
}

void StreamlineRuntime::marker(sl::PCLMarker marker) {
	if (ReflexAvailable && Token) {
		check(feature<PFun_slPCLSetMarker*>(sl::kFeaturePCL, "slPCLSetMarker")(marker, *Token), "slPCLSetMarker");
	}
}

void StreamlineRuntime::camera(const FrameGenerationInputs& f) {
	if (!Token) {
		beginFrame(static_cast<uint32_t>(f.RealFrameId));
	}
	if (Api == sl::RenderAPI::eD3D12 && DriverInterface::settings.pipelined() && Frame != static_cast<uint32_t>(f.RealFrameId)) {
		throw UnsupportedFrameGeneration("Streamline camera frame does not match the current frame token");
	}
	Camera = f;
	sl::Constants c {};
	const auto vp = f.Projection * f.View;
	c.cameraViewToClip = sdkMatrix(f.Projection);
	c.clipToCameraView = sdkMatrix(MATH::Matrix4f::Inverse(f.Projection));
	c.clipToLensClip = sdkMatrix(MATH::Matrix4f::Identity);
	const auto toPrevious = f.PreviousViewProjection * MATH::Matrix4f::Inverse(vp);
	c.clipToPrevClip = sdkMatrix(toPrevious);
	c.prevClipToClip = sdkMatrix(MATH::Matrix4f::Inverse(toPrevious));
	c.jitterOffset = { f.Jitter.x, f.Jitter.y };
	c.mvecScale = { -1, -1 };
	c.cameraPinholeOffset = { 0, 0 };
	c.cameraPos = { f.Position.x, f.Position.y, f.Position.z };
	c.cameraUp = { f.Up.x, f.Up.y, f.Up.z };
	c.cameraRight = { f.Right.x, f.Right.y, f.Right.z };
	c.cameraFwd = { f.Forward.x, f.Forward.y, f.Forward.z };
	c.cameraNear = f.Near;
	c.cameraFar = f.Far;
	c.cameraFOV = f.VerticalFov;
	c.cameraAspectRatio = float(f.DisplaySize.x) / f.DisplaySize.y;
	c.depthInverted = sl::Boolean::eFalse;
	c.cameraMotionIncluded = sl::Boolean::eTrue;
	c.motionVectors3D = sl::Boolean::eFalse;
	c.motionVectorsDilated = sl::Boolean::eFalse;
	c.motionVectorsJittered = sl::Boolean::eFalse;
	c.reset = f.Reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	c.orthographicProjection = sl::Boolean::eFalse;
	check(setConstants(c, *Token, Viewport), "slSetConstants");
	CameraReady = true;
}

void StreamlineRuntime::clearTags() {
	if (!Token) {
		return;
	}
	const sl::BufferType kinds[] = { sl::kBufferTypeDepth, sl::kBufferTypeMotionVectors, sl::kBufferTypeHUDLessColor, sl::kBufferTypeUIColorAndAlpha,
		sl::kBufferTypeBackbuffer };
	for (auto type : kinds) {
		sl::ResourceTag tag { nullptr, type, sl::ResourceLifecycle::eValidUntilPresent };
		setTags(*Token, Viewport, &tag, 1, nullptr);
	}
}

void StreamlineRuntime::commonPresentBefore() {
	if (!Initialized) {
		return;
	}
	bool skip = false;
	if (Api == sl::RenderAPI::eD3D12) {
		using Hook = int32_t (*)(void*, uint32_t, uint32_t, bool&);
		reinterpret_cast<Hook>(commonFunction("slHookPresent"))(nullptr, 0, 0, skip);
	}
	else {
		using Hook = int32_t (*)(void*, const void*, bool&);
		reinterpret_cast<Hook>(commonFunction("slHookVkPresent"))(nullptr, nullptr, skip);
	}
}

void StreamlineRuntime::commonPresentAfter() {
	if (!Initialized) {
		return;
	}
	if (Api == sl::RenderAPI::eD3D12) {
		using Hook = int32_t (*)(uint32_t);
		reinterpret_cast<Hook>(commonFunction("slHookAfterPresent"))(0);
	}
	else {
		using Hook = int32_t (*)();
		reinterpret_cast<Hook>(commonFunction("slHookVkAfterPresent"))();
	}
}

class StreamlineUpscaler final : public UpscalerInterface {
	std::shared_ptr<StreamlineRuntime> runtime;
	StreamlineCallbacks native;
	sl::DLSSOptions options;
	MATH::Vector2i render {}, display {};
	UpscalerQuality quality = UpscalerQuality::QUALITY;
	bool configured = false;

public:
	StreamlineUpscaler(std::shared_ptr<StreamlineRuntime> r, StreamlineCallbacks callbacks)
		: runtime(std::move(r))
		, native(std::move(callbacks)) {
		if (!runtime || !runtime->DLSSAvailable) {
			throw UnsupportedUpscaler(runtime ? runtime->SRReason : "Streamline runtime unavailable");
		}
	}
	~StreamlineUpscaler() override {
		try {
			if (runtime->Initialized) {
				if (native.WaitSubmitted) {
					native.WaitSubmitted();
				}
				runtime->freeResources(sl::kFeatureDLSS, runtime->Viewport);
			}
		} catch (const std::exception& e) {
			std::cerr << "DLSS SR teardown: " << e.what() << '\n';
		}
	}
	UpscalerCapabilities capabilities() const override {
		return { true, false, UpscalerType::DLSS, "DLSS Super Resolution (Streamline)", {} };
	}
	MATH::Vector2i renderSize(MATH::Vector2i d, UpscalerQuality quality) override {
		sl::DLSSOptions opt {};
		opt.mode = sdkQuality(quality);
		opt.outputWidth = d.x;
		opt.outputHeight = d.y;
		sl::DLSSOptimalSettings result {};
		try {
			StreamlineRuntime::check(
				runtime->feature<PFun_slDLSSGetOptimalSettings*>(sl::kFeatureDLSS, "slDLSSGetOptimalSettings")(opt, result), "DLSS optimal settings");
		} catch (const UnsupportedFrameGeneration& e) {
			throw UnsupportedUpscaler(e.what());
		}
		return { int(result.optimalRenderWidth), int(result.optimalRenderHeight) };
	}
	void configure(MATH::Vector2i r, MATH::Vector2i d, UpscalerQuality quality) override {
		if (configured && render == r && display == d && this->quality == quality) {
			return;
		}
		if (configured && native.WaitSubmitted) {
			native.WaitSubmitted();
		}
		options.mode = sdkQuality(quality);
		options.outputWidth = d.x;
		options.outputHeight = d.y;
		options.colorBuffersHDR = sl::Boolean::eTrue;
		options.useAutoExposure = sl::Boolean::eTrue;
		render = r;
		display = d;
		try {
			StreamlineRuntime::check(
				runtime->feature<PFun_slDLSSSetOptions*>(sl::kFeatureDLSS, "slDLSSSetOptions")(runtime->Viewport, options), "DLSS options");
		} catch (const UnsupportedFrameGeneration& e) {
			throw UnsupportedUpscaler(e.what());
		}
		this->quality = quality;
		configured = true;
	}
	MATH::Vector2f jitter(uint64_t frame, MATH::Vector2i r, MATH::Vector2i d) override {
		return UpscalerHaltonJitter(frame, std::max(8u, unsigned(8.0 * d.x * d.y / (r.x * r.y))));
	}
	void dispatch(const UpscaleFrameInputs& f) override {
		ValidateUpscalerTextures(f);
		if (!runtime->CameraReady) {
			throw UnsupportedUpscaler("Streamline camera constants missing for this real frame");
		}
		auto color = native.Resource(f.Color, false), depth = native.Resource(f.Depth, false), motion = native.Resource(f.MotionVectors, false),
			 output = native.Resource(f.Output, true);
		sl::Extent ri {}, di {};
		ri.width = f.RenderSize.x;
		ri.height = f.RenderSize.y;
		di.width = f.DisplaySize.x;
		di.height = f.DisplaySize.y;
		sl::ResourceTag tags[] = { { &color, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &ri },
			{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &ri },
			{ &motion, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &ri },
			{ &output, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &di } };
		try {
			// Evaluation-local tags keep SR's resource states independent of
			// the global tags that FG consumes later during presentation.
			const sl::BaseStructure* inputs[] = { &runtime->Viewport, &tags[0], &tags[1], &tags[2], &tags[3] };
			StreamlineRuntime::check(
				runtime->evaluate(sl::kFeatureDLSS, *runtime->Token, inputs, 5, static_cast<sl::CommandBuffer*>(native.Commands())), "DLSS evaluate");
		} catch (const UnsupportedFrameGeneration& e) {
			native.Invalidate();
			throw UnsupportedUpscaler(e.what());
		}
		native.Invalidate();
	}
};

class StreamlineFrameGeneration final : public FrameGenerationInterface {
	std::shared_ptr<StreamlineRuntime> runtime;
	StreamlineCallbacks native;
	FrameGenerationStatus state;
	FrameGenerationInputs last;
	MATH::Vector2i render {}, display {};
	unsigned multiplier = 0;
	bool attached = false, prepared = false, enabledOptions = false;
	struct PresentReceipt {
		FrameGenerationInputs Inputs;
		void* Fence = nullptr;
		uint64_t FenceValue = 0;
		std::shared_ptr<void> FenceOwner;
	};
	std::deque<PresentReceipt> receipts;
	bool receiptCaptured = false;

	void capturePresent() {
		if (!prepared || receiptCaptured) {
			return;
		}
		sl::DLSSGState result {};
		StreamlineRuntime::check(runtime->feature<PFun_slDLSSGGetState*>(sl::kFeatureDLSS_G, "slDLSSGGetState")(runtime->Viewport, result, nullptr),
			"DLSS FG completion state");
		auto* fence = result.inputsProcessingCompletionFence;
		receipts.push_back({ last, fence, result.lastPresentInputsProcessingCompletionFenceValue,
			native.RetainInputsFence && fence ? native.RetainInputsFence(fence) : nullptr });
		receiptCaptured = true;
		++state.RealFrames;
		state.PresentedFrames += result.numFramesActuallyPresented;
		state.GeneratedFrames = state.PresentedFrames > state.RealFrames ? state.PresentedFrames - state.RealFrames : 0;
		std::cout << "FRAMEGEN_PRESENT DLSS real=" << state.RealFrames << " generated=" << state.GeneratedFrames
				  << " presented=" << state.PresentedFrames << '\n';
	}

public:
	StreamlineFrameGeneration(std::shared_ptr<StreamlineRuntime> r, StreamlineCallbacks callbacks)
		: runtime(std::move(r))
		, native(std::move(callbacks)) {
		if (!runtime || !runtime->FGAvailable) {
			throw UnsupportedFrameGeneration(runtime ? runtime->FGReason : "Streamline runtime unavailable");
		}
		state.Technology = FrameGenerationType::DLSS;
		state.Implementation = "DLSS Frame Generation (Streamline 2.14.1)";
	}

	~StreamlineFrameGeneration() override {
		try {
			suspend();
			runtime->clearTags();
			if (runtime->Initialized) {
				runtime->freeResources(sl::kFeatureDLSS_G, runtime->Viewport);
			}
			if (attached) {
				native.Presenter(false);
			}
		} catch (const std::exception& e) {
			std::cerr << "DLSS FG teardown: " << e.what() << '\n';
		}
	}

	void configure(const FrameGenerationSettings& settings, MATH::Vector2i r, MATH::Vector2i d) override {
		if (enabledOptions && multiplier == settings.multiplier && render == r && display == d) {
			return;
		}
		sl::DLSSGState caps {};
		StreamlineRuntime::check(
			runtime->feature<PFun_slDLSSGGetState*>(sl::kFeatureDLSS_G, "slDLSSGGetState")(runtime->Viewport, caps, nullptr), "DLSS FG capabilities");
		state.MaxMultiplier = caps.numFramesToGenerateMax + 1;
		if (settings.multiplier > state.MaxMultiplier) {
			throw UnsupportedFrameGeneration("Requested DLSS MFG multiplier is unsupported");
		}
		if (d.x < static_cast<int>(caps.minWidthOrHeight) || d.y < static_cast<int>(caps.minWidthOrHeight)) {
			throw UnsupportedFrameGeneration("Display size is below the DLSS FG minimum supported dimension");
		}
		if (settings.vsync && (runtime->Api == sl::RenderAPI::eVulkan || caps.bIsVsyncSupportAvailable != sl::Boolean::eTrue)) {
			throw UnsupportedFrameGeneration("DLSS FG VSync is unavailable for this backend/runtime");
		}
		sl::DLSSGOptions options {};
		options.mode = sl::DLSSGMode::eOn;
		options.numFramesToGenerate = settings.multiplier - 1;
		options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
		options.queueParallelismMode = sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue;
		options.enableUserInterfaceRecomposition = runtime->Api == sl::RenderAPI::eVulkan ? sl::Boolean::eFalse : sl::Boolean::eTrue;
		options.onErrorCallback = sdkError;
		StreamlineRuntime::check(
			runtime->feature<PFun_slDLSSGSetOptions*>(sl::kFeatureDLSS_G, "slDLSSGSetOptions")(runtime->Viewport, options), "DLSS FG options");
		enabledOptions = true;
		if (!attached) {
			native.Presenter(true);
			attached = true;
		}
		render = r;
		display = d;
		multiplier = settings.multiplier;
		state.Multiplier = multiplier;
		state.Supported = true;
		std::cout << "FRAMEGEN_CONTEXT DLSS x" << multiplier << " " << d.x << "x" << d.y << '\n';
	}

	void prepare(const FrameGenerationInputs& f) override {
		if (!runtime->CameraReady) {
			throw UnsupportedFrameGeneration("DLSS FG camera constants missing");
		}
		if (const auto e = apiError.exchange(0)) {
			throw UnsupportedFrameGeneration("DLSS FG asynchronous presentation error: " + std::to_string(e));
		}
		last = f;
		auto color = native.Resource(f.Color, false), depth = native.Resource(f.Depth, false), motion = native.Resource(f.MotionVectors, false),
			 ui = native.Resource(f.UI, false);
		sl::Extent ri {}, di {};
		ri.width = f.RenderSize.x;
		ri.height = f.RenderSize.y;
		di.width = f.DisplaySize.x;
		di.height = f.DisplaySize.y;
		sl::ResourceTag tags[] = { { &color, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &di },
			{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &ri },
			{ &motion, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &ri },
			{ &ui, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent, &di },
			{ nullptr, sl::kBufferTypeBackbuffer, sl::ResourceLifecycle::eValidUntilPresent, &di } };
		StreamlineRuntime::check(
			runtime->setTags(*runtime->Token, runtime->Viewport, tags, 5, static_cast<sl::CommandBuffer*>(native.Commands())), "DLSS FG tags");
		native.Invalidate();
		prepared = true;
		state.Active = true;
		if (state.RealFrames == 0 || f.Reset) {
			std::cout << "FRAMEGEN_PREPARE DLSS frame=" << f.RealFrameId << " reset=" << f.Reset << '\n';
		}
	}

	void suspend() override {
		if (runtime->Initialized) {
			waitForPresents();
			sl::DLSSGOptions options {};
			options.mode = sl::DLSSGMode::eOff;
			options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
			runtime->feature<PFun_slDLSSGSetOptions*>(sl::kFeatureDLSS_G, "slDLSSGSetOptions")(runtime->Viewport, options);
			runtime->clearTags();
			// SetOptions is deferred until Present. Drain it before changing
			// the HWND or releasing the swapchain used by the SDK pacer.
			if (enabledOptions && attached && native.ApplyOff) {
				native.ApplyOff();
			}
			receiptCaptured = false;
		}
		state.Active = false;
		prepared = false;
		enabledOptions = false;
	}

	void waitForPresents() override {
		if (!runtime->Initialized || !attached) {
			return;
		}
		// Vulkan's serial present path waits before afterPresent(). D3D12
		// captures immediately after Present, so a drain must not mistake a
		// prepared but unpresented frame for a completed present.
		if (runtime->Api == sl::RenderAPI::eVulkan) {
			capturePresent();
		}
		while (!receipts.empty()) {
			const auto& receipt = receipts.front();
			native.WaitInputs(receipt.Fence, receipt.FenceValue);
			receipts.pop_front();
		}
	}

	void waitForFrame(uint64_t realFrameId) override {
		for (auto receipt = receipts.begin(); receipt != receipts.end(); ++receipt) {
			if (receipt->Inputs.RealFrameId == realFrameId) {
				native.WaitInputs(receipt->Fence, receipt->FenceValue);
				receipts.erase(receipt);
				return;
			}
		}
	}

	void afterPresent() override {
		capturePresent();
		prepared = false;
		last = {};
		receiptCaptured = false;
	}

	FrameGenerationStatus status() const override {
		return state;
	}
};

std::unique_ptr<UpscalerInterface> CreateStreamlineUpscaler(std::shared_ptr<StreamlineRuntime> r, StreamlineCallbacks c) {
	return std::make_unique<StreamlineUpscaler>(std::move(r), std::move(c));
}

std::unique_ptr<FrameGenerationInterface> CreateStreamlineFrameGeneration(std::shared_ptr<StreamlineRuntime> r, StreamlineCallbacks c) {
	return std::make_unique<StreamlineFrameGeneration>(std::move(r), std::move(c));
}
}
#endif
