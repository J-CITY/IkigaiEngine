#include "driverDx12.h"
#ifdef DX12_BACKEND
#include "textureDx12.h"
#ifdef IKIGAI_USE_STREAMLINE
#include "../streamlineRuntime.h"
#endif
#if defined(IKIGAI_USE_FSR) || defined(IKIGAI_USE_DLSS)
#include "../upscalerSdkUtils.h"
#ifdef IKIGAI_USE_FSR
#include <ffx_upscale.h>
#include <dx12/ffx_api_dx12.h>
#endif
#if defined(IKIGAI_USE_DLSS) && !defined(IKIGAI_USE_STREAMLINE)
#include <nvsdk_ngx_helpers.h>
#endif
using namespace IKIGAI::RENDER;
using namespace IKIGAI;
namespace {
using UpscalerDriver=DriverDx12;
void beginUpscaler(DriverDx12&) {}
void endUpscaler(DriverDx12& driver) {
    auto barrier=CD3DX12_RESOURCE_BARRIER::UAV(nullptr);driver.getCommandList()->ResourceBarrier(1,&barrier);
    driver.invalidateUpscalerState();
}
void* upscalerCommand(DriverDx12& driver) {return driver.getCommandList().Get();}
void deferUpscaler(DriverDx12& driver,std::shared_ptr<void> context) {
    if(!driver.mShuttingDown)driver.mRayDeferred.push_back(std::move(context));
}
#define IKIGAI_PIN_DX12_UPSCALER_CONTEXT
void pinUpscalerContext(DriverDx12& driver, const std::shared_ptr<void>& context) {
	driver.mRayFrameResources.push_back(context);
}
std::shared_ptr<TextureDx12> upscaleTexture(DriverDx12& driver,const std::shared_ptr<TextureInterface>& texture,bool write) {
    auto t=std::dynamic_pointer_cast<TextureDx12>(texture);
    if(!t)throw std::runtime_error("UPSCALE requires DX12 textures");
    t->setState(driver.getCommandList().Get(),write?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    driver.mRayFrameResources.push_back(t);return t;
}
#ifdef IKIGAI_USE_FSR
using FsrBackendDescriptor=ffxCreateBackendDX12Desc;
constexpr auto FsrLibrary=L"amd_fidelityfx_loader_dx12.dll";
FsrBackendDescriptor fsrBackend(DriverDx12& d) {
    FsrBackendDescriptor b{};b.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;b.device=d.getDevice().Get();return b;
}
FfxApiResource fsrResource(DriverDx12& driver,const std::shared_ptr<TextureInterface>& texture,bool write) {
    if(!texture)return {};
    auto t=upscaleTexture(driver,texture,write);
    auto resource=ffxApiGetResourceDX12(t->getResource().Get(),
        write?FFX_API_RESOURCE_STATE_UNORDERED_ACCESS:FFX_API_RESOURCE_STATE_COMPUTE_READ);
    if(texture->getType()==TextureType::DEPTH)resource.description.usage|=FFX_API_RESOURCE_USAGE_DEPTHTARGET;
    return resource;
}
#include "../fsrUpscalerCommon.h"
#endif
#if defined(IKIGAI_USE_DLSS) && !defined(IKIGAI_USE_STREAMLINE)
#define NGX_CALL(name) NVSDK_NGX_D3D12_##name
NVSDK_NGX_Result initNgx(DriverDx12& d,const wchar_t* cache) {
    return NVSDK_NGX_D3D12_Init_with_ProjectID(IKIGAI_DLSS_PROJECT_ID,NVSDK_NGX_ENGINE_TYPE_CUSTOM,"1.0",cache,d.getDevice().Get());
}
void shutdownNgx(DriverDx12& d) {NVSDK_NGX_D3D12_Shutdown1(d.getDevice().Get());}
NVSDK_NGX_Result createNgx(DriverDx12& d,NVSDK_NGX_Handle** h,NVSDK_NGX_Parameter* p,NVSDK_NGX_DLSS_Create_Params* desc) {
    return NGX_D3D12_CREATE_DLSS_EXT(d.getCommandList().Get(),1,1,h,p,desc);
}
NVSDK_NGX_Result evaluateNgx(DriverDx12& d,NVSDK_NGX_Handle* handle,NVSDK_NGX_Parameter* params,const UpscaleFrameInputs& f) {
    auto color=upscaleTexture(d,f.Color,false),depth=upscaleTexture(d,f.Depth,false),motion=upscaleTexture(d,f.MotionVectors,false),out=upscaleTexture(d,f.Output,true);
    NVSDK_NGX_D3D12_DLSS_Eval_Params p{};
    p.Feature.pInColor=color->getResource().Get();p.Feature.pInOutput=out->getResource().Get();
    p.pInDepth=depth->getResource().Get();p.pInMotionVectors=motion->getResource().Get();
    p.InJitterOffsetX=f.Jitter.x;p.InJitterOffsetY=f.Jitter.y;
    p.InMVScaleX=-float(f.RenderSize.x);p.InMVScaleY=-float(f.RenderSize.y);
    p.InReset=f.Reset;p.InPreExposure=f.PreExposure;p.InExposureScale=1;
    p.InRenderSubrectDimensions={uint32_t(f.RenderSize.x),uint32_t(f.RenderSize.y)};
    return NGX_D3D12_EVALUATE_DLSS_EXT(d.getCommandList().Get(),handle,params,&p);
}
#include "../dlssUpscalerCommon.h"
#undef NGX_CALL
#endif
#undef IKIGAI_PIN_DX12_UPSCALER_CONTEXT
}
#endif
std::unique_ptr<IKIGAI::RENDER::UpscalerInterface> IKIGAI::RENDER::DriverDx12::createUpscaler(UpscalerType type) {
#ifdef IKIGAI_USE_STREAMLINE
    if(type==UpscalerType::DLSS){try{return CreateStreamlineUpscaler(std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime),streamlineCallbacks());}catch(const UnsupportedFrameGeneration& e){throw UnsupportedUpscaler(e.what());}}
#endif

#ifdef IKIGAI_USE_FSR
    if(type==UpscalerType::FSR)return std::make_unique<FsrUpscaler>(*this);
#endif
#if defined(IKIGAI_USE_DLSS) && !defined(IKIGAI_USE_STREAMLINE)
    if(type==UpscalerType::DLSS)return std::make_unique<DlssUpscaler>(*this);
#endif
    throw UnsupportedUpscaler("Requested upscaler SDK is not compiled for DX12");
}
void IKIGAI::RENDER::DriverDx12::invalidateUpscalerState() { mDirty.set(); }
#endif
