// Included inside each backend's anonymous namespace after native NGX helpers.
void ngxCheck(NVSDK_NGX_Result code, const char* call) {
	if (NVSDK_NGX_FAILED(code))
		throw UnsupportedUpscaler(std::string(call) + " failed: " + std::to_string(static_cast<unsigned>(code)));
}
NVSDK_NGX_PerfQuality_Value ngxQuality(UpscalerQuality quality) {
	switch (quality) {
	case UpscalerQuality::QUALITY:
		return NVSDK_NGX_PerfQuality_Value_MaxQuality;
	case UpscalerQuality::BALANCED:
		return NVSDK_NGX_PerfQuality_Value_Balanced;
	case UpscalerQuality::PERFORMANCE:
		return NVSDK_NGX_PerfQuality_Value_MaxPerf;
	}
	throw std::runtime_error("Invalid DLSS quality");
}
struct NgxRuntime {
	UpscalerDriver& Driver;
	std::shared_ptr<UpscalerModule> Module;
	NVSDK_NGX_Parameter* Caps = nullptr;
	bool Initialized = false;
	explicit NgxRuntime(UpscalerDriver& driver)
		: Driver(driver)
		, Module(std::make_shared<UpscalerModule>(L"nvngx_dlss.dll")) {
		auto cache = UpscalerExecutableDirectory() / "ngx-cache";
		std::filesystem::create_directories(cache);
		ngxCheck(initNgx(driver, cache.c_str()), "NGX initialize");
		Initialized = true;
	}
	~NgxRuntime() {
		if (Caps)
			NGX_CALL(DestroyParameters)(Caps);
		if (Initialized)
			shutdownNgx(Driver);
	}
	void checkSupport() {
		if (!Caps)
			ngxCheck(NGX_CALL(GetCapabilityParameters)(&Caps), "NGX capability query");
		int available = 0;
		ngxCheck(Caps->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &available), "DLSS availability");
		if (!available)
			throw UnsupportedUpscaler("DLSS Super Resolution unavailable on the selected GPU/driver");
	}
};
struct NgxContext {
	std::shared_ptr<NgxRuntime> Runtime;
	NVSDK_NGX_Parameter* Parameters = nullptr;
	NVSDK_NGX_Handle* Feature = nullptr;
	NVSDK_NGX_DLSS_Create_Params Description { };
	~NgxContext() {
		if (Feature)
			NGX_CALL(ReleaseFeature)(Feature);
		if (Parameters)
			NGX_CALL(DestroyParameters)(Parameters);
	}
};
class DlssUpscaler final : public UpscalerInterface {
	UpscalerDriver& mDriver;
	std::shared_ptr<NgxRuntime> mRuntime;
	std::shared_ptr<NgxContext> mContext;
	MATH::Vector2i mRender { }, mDisplay { };
	UpscalerQuality mQuality = UpscalerQuality::QUALITY;
	void release() {
		if (mContext)
			deferUpscaler(mDriver, std::move(mContext));
	}

public:
	explicit DlssUpscaler(UpscalerDriver& driver)
		: mDriver(driver) {
		if (!driver.mDlssRuntime)
			driver.mDlssRuntime = std::make_shared<NgxRuntime>(driver);
		mRuntime = std::static_pointer_cast<NgxRuntime>(driver.mDlssRuntime);
		mRuntime->checkSupport();
	}
	~DlssUpscaler() override {
		release();
	}
	UpscalerCapabilities capabilities() const override {
		return { true, false, UpscalerType::DLSS, "DLSS Super Resolution (NGX)", { } };
	}
	MATH::Vector2i renderSize(MATH::Vector2i display, UpscalerQuality quality) override {
		unsigned w = 0, h = 0, maxW = 0, maxH = 0, minW = 0, minH = 0;
		float sharpness = 0;
		ngxCheck(
			NGX_DLSS_GET_OPTIMAL_SETTINGS(mRuntime->Caps, display.x, display.y, ngxQuality(quality), &w, &h, &maxW, &maxH, &minW, &minH, &sharpness),
			"DLSS optimal settings");
		return { static_cast<int>(w), static_cast<int>(h) };
	}
	void configure(MATH::Vector2i render, MATH::Vector2i display, UpscalerQuality quality) override {
		if (mContext && render == mRender && display == mDisplay && quality == mQuality)
			return;
		auto context = std::make_shared<NgxContext>();
		context->Runtime = mRuntime;
		ngxCheck(NGX_CALL(AllocateParameters)(&context->Parameters), "NGX allocate parameters");
		auto& d = context->Description;
		d.Feature.InWidth = render.x;
		d.Feature.InHeight = render.y;
		d.Feature.InTargetWidth = display.x;
		d.Feature.InTargetHeight = display.y;
		d.Feature.InPerfQualityValue = ngxQuality(quality);
		d.InFeatureCreateFlags
			= NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
		release();
		mContext = std::move(context);
		mRender = render;
		mDisplay = display;
		mQuality = quality;
		std::cout << "UPSCALER_CONTEXT DLSS " << render.x << "x" << render.y << " -> " << display.x << "x" << display.y << '\n';
	}
	MATH::Vector2f jitter(uint64_t frame, MATH::Vector2i render, MATH::Vector2i display) override {
		const float ratio = float(display.x) / render.x;
		return UpscalerHaltonJitter(frame, std::max(8u, static_cast<unsigned>(std::ceil(8 * ratio * ratio))));
	}
	void dispatch(const UpscaleFrameInputs& f) override {
		ValidateUpscalerTextures(f);
		if (!mContext || f.RenderSize != mRender || f.DisplaySize != mDisplay)
			throw std::runtime_error("DLSS context dimensions mismatch");
#ifdef IKIGAI_PIN_DX12_UPSCALER_CONTEXT
		// NGX feature creation and evaluation both record work into this frame.
		pinUpscalerContext(mDriver, mContext);
#endif
		beginUpscaler(mDriver);
		if (!mContext->Feature) {
			const auto result = createNgx(mDriver, &mContext->Feature, mContext->Parameters, &mContext->Description);
			if (NVSDK_NGX_FAILED(result)) {
				endUpscaler(mDriver);
				ngxCheck(result, "NGX create DLSS");
			}
		}
		const auto result = evaluateNgx(mDriver, mContext->Feature, mContext->Parameters, f);
		endUpscaler(mDriver);
		ngxCheck(result, "NGX evaluate DLSS");
	}
};
