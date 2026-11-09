#pragma once
#ifdef IKIGAI_USE_STREAMLINE
#include "interface/frameGenerationInterface.h"
#include "upscalerSdkUtils.h"
#include <functional>
#include <sl.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_pcl.h>
#include <sl_reflex.h>

namespace IKIGAI::RENDER {
class StreamlineRuntime {
	std::shared_ptr<UpscalerModule> module;
	std::shared_ptr<UpscalerModule> commonModule;
	void* (*commonFunction)(const char*) {};
	PFun_slShutdown* shutdown {};

public:
	sl::RenderAPI Api;
	PFun_slIsFeatureSupported* supported {};
	PFun_slSetFeatureLoaded* loadFeature {};
	PFun_slGetFeatureRequirements* requirements {};
	PFun_slUpgradeInterface* upgrade {};
	PFun_slGetNativeInterface* nativeInterface {};
	PFun_slSetD3DDevice* setD3DDevice {};
	PFun_slGetFeatureFunction* featureFunction {};
	PFun_slGetNewFrameToken* getToken {};
	PFun_slSetConstants* setConstants {};
	PFun_slSetTagForFrame* setTags {};
	PFun_slEvaluateFeature* evaluate {};
	PFun_slFreeResources* freeResources {};
	sl::ViewportHandle Viewport { 0 };
	sl::FrameToken* Token = nullptr;
	FrameGenerationInputs Camera;
	bool CameraReady = false, Initialized = false, ReflexAvailable = false, DLSSAvailable = false, FGAvailable = false;
	uint32_t Frame = 0;
	std::string FGReason, SRReason;
	explicit StreamlineRuntime(sl::RenderAPI);
	~StreamlineRuntime();
	void close();
	void deviceReady(const sl::AdapterInfo&);
	void beginFrame(uint32_t);
	void marker(sl::PCLMarker);
	void camera(const FrameGenerationInputs&);
	void clearTags();
	void commonPresentBefore();
	void commonPresentAfter();
	template <class T> T feature(sl::Feature f, const char* name) {
		void* address = nullptr;
		check(featureFunction(f, name, address), name);
		return reinterpret_cast<T>(address);
	}
	template <class T> T symbol(const char* name) {
		return module->function<T>(name);
	}
	static void check(sl::Result result, const char* call) {
		if (result != sl::Result::eOk) {
			throw UnsupportedFrameGeneration(std::string(call) + ": Streamline error " + std::to_string(static_cast<int>(result)));
		}
	}
};
std::shared_ptr<StreamlineRuntime> AcquireStreamline(sl::RenderAPI);
struct StreamlineCallbacks {
	std::function<sl::Resource(const std::shared_ptr<TextureInterface>&, bool)> Resource;
	std::function<void*()> Commands;
	std::function<void()> Invalidate;
	std::function<void(bool)> Presenter; // Ensure or restore native presentation.
	std::function<void(void*, uint64_t)> WaitInputs;
	std::function<void()> ApplyOff;
	std::function<void()> WaitSubmitted;
	// Keep the SDK fence alive until the corresponding input resources are reusable.
	std::function<std::shared_ptr<void>(void*)> RetainInputsFence;
};
std::unique_ptr<UpscalerInterface> CreateStreamlineUpscaler(std::shared_ptr<StreamlineRuntime>, StreamlineCallbacks);
std::unique_ptr<FrameGenerationInterface> CreateStreamlineFrameGeneration(std::shared_ptr<StreamlineRuntime>, StreamlineCallbacks);
}
#endif
