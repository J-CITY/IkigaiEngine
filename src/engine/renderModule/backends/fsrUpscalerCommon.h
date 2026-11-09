// Included inside each backend's anonymous namespace after its FFX API headers.
struct FsrApi {
	std::shared_ptr<UpscalerModule> Module;
	PfnFfxCreateContext Create;
	PfnFfxDestroyContext Destroy;
	PfnFfxQuery Query;
	PfnFfxDispatch Dispatch;
	explicit FsrApi(const wchar_t* library)
		: Module(std::make_shared<UpscalerModule>(library)) {
		Create = Module->function<PfnFfxCreateContext>("ffxCreateContext");
		Destroy = Module->function<PfnFfxDestroyContext>("ffxDestroyContext");
		Query = Module->function<PfnFfxQuery>("ffxQuery");
		Dispatch = Module->function<PfnFfxDispatch>("ffxDispatch");
	}
};
void fsrCheck(ffxReturnCode_t code, const char* call) {
	if (code)
		throw UnsupportedUpscaler(std::string(call) + " failed: " + std::to_string(code));
}
void fsrMessage(uint32_t, const wchar_t* message) {
	std::wcerr << L"FSR: " << message << L'\n';
}
struct FsrContext {
	std::shared_ptr<FsrApi> Api;
	ffxContext Context { };
	FsrBackendDescriptor Backend { };
	ffxCreateContextDescUpscale Description { };
#ifdef FFX_UPSCALER_VERSION
	ffxCreateContextDescUpscaleVersion Version { };
#endif
	~FsrContext() {
		if (Context)
			Api->Destroy(&Context, nullptr);
	}
};
class FsrUpscaler final : public UpscalerInterface {
	UpscalerDriver& mDriver;
	std::shared_ptr<FsrApi> mApi;
	std::shared_ptr<FsrContext> mContext;
	MATH::Vector2i mRender { }, mDisplay { };
	UpscalerQuality mQuality = UpscalerQuality::QUALITY;
	std::string mImplementation = "FSR";
	void release() {
		if (mContext)
			deferUpscaler(mDriver, std::move(mContext));
	}

public:
	explicit FsrUpscaler(UpscalerDriver& driver)
		: mDriver(driver)
		, mApi(std::make_shared<FsrApi>(FsrLibrary)) { }
	~FsrUpscaler() override {
		release();
	}
	UpscalerCapabilities capabilities() const override {
		return { true, true, UpscalerType::FSR, mImplementation, { } };
	}
	MATH::Vector2i renderSize(MATH::Vector2i display, UpscalerQuality quality) override {
		uint32_t w = 0, h = 0;
		ffxQueryDescUpscaleGetRenderResolutionFromQualityMode query { };
		query.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE;
		query.displayWidth = display.x;
		query.displayHeight = display.y;
		query.qualityMode = static_cast<uint32_t>(quality) + 1;
		query.pOutRenderWidth = &w;
		query.pOutRenderHeight = &h;
		fsrCheck(mApi->Query(nullptr, &query.header), "FSR render size query");
		return { static_cast<int>(w), static_cast<int>(h) };
	}
	void configure(MATH::Vector2i render, MATH::Vector2i display, UpscalerQuality quality) override {
		if (mContext && render == mRender && display == mDisplay && quality == mQuality)
			return;
		auto context = std::make_shared<FsrContext>();
		context->Api = mApi;
		context->Backend = fsrBackend(mDriver);
		context->Description.header = { FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE, &context->Backend.header };
#ifdef FFX_UPSCALER_VERSION
		context->Version.header = { FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION, &context->Backend.header };
		context->Version.version = FFX_UPSCALER_VERSION;
		context->Description.header.pNext = &context->Version.header;
#endif
		context->Description.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
		context->Description.maxRenderSize = { uint32_t(render.x), uint32_t(render.y) };
		context->Description.maxUpscaleSize = { uint32_t(display.x), uint32_t(display.y) };
		context->Description.fpMessage = fsrMessage;
		fsrCheck(mApi->Create(&context->Context, &context->Description.header, nullptr), "FSR create context");
		ffxQueryGetProviderVersion version { };
		version.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
		if (!mApi->Query(&context->Context, &version.header) && version.versionName)
			mImplementation = version.versionName;
		release();
		mContext = std::move(context);
		mRender = render;
		mDisplay = display;
		mQuality = quality;
		std::cout << "UPSCALER_CONTEXT FSR " << mImplementation << " " << render.x << "x" << render.y << " -> " << display.x << "x" << display.y
				  << '\n';
	}
	MATH::Vector2f jitter(uint64_t frame, MATH::Vector2i render, MATH::Vector2i display) override {
		int32_t phases = 0;
		ffxQueryDescUpscaleGetJitterPhaseCount count { };
		count.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
		count.renderWidth = render.x;
		count.displayWidth = display.x;
		count.pOutPhaseCount = &phases;
		fsrCheck(mApi->Query(nullptr, &count.header), "FSR jitter phases");
		if (phases < 1)
			throw UnsupportedUpscaler("Invalid FSR jitter phases");
		MATH::Vector2f result { };
		ffxQueryDescUpscaleGetJitterOffset offset { };
		offset.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
		offset.index = static_cast<int32_t>(frame % phases);
		offset.phaseCount = phases;
		offset.pOutX = &result.x;
		offset.pOutY = &result.y;
		fsrCheck(mApi->Query(nullptr, &offset.header), "FSR jitter offset");
		return result;
	}
	void dispatch(const UpscaleFrameInputs& f) override {
		ValidateUpscalerTextures(f);
		if (!mContext || f.RenderSize != mRender || f.DisplaySize != mDisplay)
			throw std::runtime_error("FSR context dimensions mismatch");
		beginUpscaler(mDriver);
		ffxDispatchDescUpscale d { };
		d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
		d.commandList = upscalerCommand(mDriver);
		d.color = fsrResource(mDriver, f.Color, false);
		d.depth = fsrResource(mDriver, f.Depth, false);
		d.motionVectors = fsrResource(mDriver, f.MotionVectors, false);
		d.output = fsrResource(mDriver, f.Output, true);
		d.exposure = fsrResource(mDriver, f.Exposure, false);
		d.reactive = fsrResource(mDriver, f.Reactive, false);
		d.transparencyAndComposition = fsrResource(mDriver, f.Transparency, false);
		d.jitterOffset = { f.Jitter.x, f.Jitter.y };
		d.motionVectorScale = { -float(f.RenderSize.x), -float(f.RenderSize.y) };
		d.renderSize = { uint32_t(f.RenderSize.x), uint32_t(f.RenderSize.y) };
		d.upscaleSize = { uint32_t(f.DisplaySize.x), uint32_t(f.DisplaySize.y) };
		d.enableSharpening = f.Sharpness > 0;
		d.sharpness = f.Sharpness;
		d.frameTimeDelta = f.DeltaMilliseconds;
		d.preExposure = f.PreExposure;
		d.reset = f.Reset;
		d.cameraNear = f.Near;
		d.cameraFar = f.Far;
		d.cameraFovAngleVertical = f.VerticalFov;
		d.viewSpaceToMetersFactor = 1;
#ifdef IKIGAI_PIN_DX12_UPSCALER_CONTEXT
		// The command list can outlive a later reconfiguration of the upscaler.
		pinUpscalerContext(mDriver, mContext);
#endif
		const auto code = mApi->Dispatch(&mContext->Context, &d.header);
		endUpscaler(mDriver);
		fsrCheck(code, "FSR dispatch");
	}
};
