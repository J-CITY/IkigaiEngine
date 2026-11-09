#include "driverVk.h"
#ifdef VULKAN_BACKEND
#include "textureVk.h"
#ifdef IKIGAI_USE_STREAMLINE
#include "../streamlineRuntime.h"
#endif
#if defined(_WIN32) && (defined(IKIGAI_USE_FSR) || defined(IKIGAI_USE_DLSS))
#include "../upscalerSdkUtils.h"
#ifdef IKIGAI_USE_FSR
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/vk/ffx_api_vk.h>
#endif
#if defined(IKIGAI_USE_DLSS) && !defined(IKIGAI_USE_STREAMLINE)
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_vk.h>
#endif
using namespace IKIGAI::RENDER;
using namespace IKIGAI;
namespace {
using UpscalerDriver=DriverVk;
VkFormat upscaleFormat(PixelFormat format) {
    switch(format) {
    case PixelFormat::RGBA_FLOAT:return VK_FORMAT_R32G32B32A32_SFLOAT;
    case PixelFormat::RG_FLOAT:return VK_FORMAT_R32G32_SFLOAT;
    case PixelFormat::R_FLOAT:return VK_FORMAT_R32_SFLOAT;
    case PixelFormat::R_INT:return VK_FORMAT_R8_UNORM;
    case PixelFormat::DEPTH32_FLOAT:return VK_FORMAT_D32_SFLOAT;
    default:throw std::runtime_error("Unsupported UPSCALE Vulkan texture format");
    }
}
void upscaleBarrier(DriverVk& driver) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(*driver.getCurrentFrame().mCommandBuffer,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&barrier,0,nullptr,0,nullptr);
}
void beginUpscaler(DriverVk& driver) {driver.deactivateRenderPass();upscaleBarrier(driver);}
void endUpscaler(DriverVk& driver) {
	upscaleBarrier(driver);
	driver.mBoundPipelineKey.reset();
}
void* upscalerCommand(DriverVk& driver) {return reinterpret_cast<void*>(VkCommandBuffer(*driver.getCurrentFrame().mCommandBuffer));}
void deferUpscaler(DriverVk& driver,std::shared_ptr<void> context) {driver.destroyDeferred(std::move(context));}
std::shared_ptr<TextureVk> upscaleTexture(DriverVk& driver,const std::shared_ptr<TextureInterface>& texture,bool write) {
    auto native=std::dynamic_pointer_cast<TextureVk>(texture);
    if(!native)throw std::runtime_error("UPSCALE requires Vulkan textures");
    native->setState(driver.getCurrentFrame().mCommandBuffer,write?vk::ImageLayout::eGeneral:vk::ImageLayout::eShaderReadOnlyOptimal);
    driver.mRayTracingFrameResources.push_back(native);
    return native;
}
#ifdef IKIGAI_USE_FSR
using FsrBackendDescriptor=ffxCreateBackendVKDesc;
constexpr auto FsrLibrary=L"amd_fidelityfx_vk.dll";
FsrBackendDescriptor fsrBackend(DriverVk& d) {
    FsrBackendDescriptor b{};b.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    b.vkDevice=*d.mDevice;b.vkPhysicalDevice=*d.mPhysicalDevice;b.vkDeviceProcAddr=vkGetDeviceProcAddr;return b;
}
FfxApiResource fsrResource(DriverVk& driver,const std::shared_ptr<TextureInterface>& texture,bool write) {
    if(!texture)return {};
    auto native=upscaleTexture(driver,texture,write);
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType=VK_IMAGE_TYPE_2D;info.format=upscaleFormat(texture->getFormat());
    info.extent={uint32_t(texture->getWidth()),uint32_t(texture->getHeight()),1};
    info.mipLevels=texture->getMipCount();info.arrayLayers=1;
    info.usage=write?VK_IMAGE_USAGE_STORAGE_BIT:VK_IMAGE_USAGE_SAMPLED_BIT;
    return ffxApiGetResourceVK(reinterpret_cast<void*>(VkImage(native->mImagePtr)),
        ffxApiGetImageResourceDescriptionVK(native->mImagePtr,info,0),
        write?FFX_API_RESOURCE_STATE_UNORDERED_ACCESS:FFX_API_RESOURCE_STATE_COMPUTE_READ);
}
#include "../fsrUpscalerCommon.h"
#endif
#if defined(IKIGAI_USE_DLSS) && !defined(IKIGAI_USE_STREAMLINE)
#define NGX_CALL(name) NVSDK_NGX_VULKAN_##name
NVSDK_NGX_FeatureDiscoveryInfo ngxDiscovery() {
    NVSDK_NGX_FeatureDiscoveryInfo d{};d.SDKVersion=NVSDK_NGX_Version_API;d.FeatureID=NVSDK_NGX_Feature_SuperSampling;
    d.Identifier.IdentifierType=NVSDK_NGX_Application_Identifier_Type_Project_Id;
    d.Identifier.v.ProjectDesc={IKIGAI_DLSS_PROJECT_ID,NVSDK_NGX_ENGINE_TYPE_CUSTOM,"1.0"};
    d.ApplicationDataPath=L".";return d;
}
NVSDK_NGX_Result initNgx(DriverVk& d,const wchar_t* cache) {
    return NVSDK_NGX_VULKAN_Init_with_ProjectID(IKIGAI_DLSS_PROJECT_ID,NVSDK_NGX_ENGINE_TYPE_CUSTOM,"1.0",cache,
        *d.mInstance,*d.mPhysicalDevice,*d.mDevice,vkGetInstanceProcAddr,vkGetDeviceProcAddr);
}
void shutdownNgx(DriverVk& d) {NVSDK_NGX_VULKAN_Shutdown1(*d.mDevice);}
NVSDK_NGX_Result createNgx(DriverVk& d,NVSDK_NGX_Handle** h,NVSDK_NGX_Parameter* p,NVSDK_NGX_DLSS_Create_Params* desc) {
    return NGX_VULKAN_CREATE_DLSS_EXT1(*d.mDevice,*d.getCurrentFrame().mCommandBuffer,1,1,h,p,desc);
}
NVSDK_NGX_Resource_VK ngxResource(DriverVk& d,const std::shared_ptr<TextureInterface>& texture,bool write) {
    auto t=upscaleTexture(d,texture,write);
    VkImageSubresourceRange range{VkImageAspectFlags(texture->getFormat()==PixelFormat::DEPTH32_FLOAT?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,1,0,1};
    return NVSDK_NGX_Create_ImageView_Resource_VK(*t->mImageView,t->mImagePtr,range,upscaleFormat(texture->getFormat()),
        texture->getWidth(),texture->getHeight(),write);
}
NVSDK_NGX_Result evaluateNgx(DriverVk& d,NVSDK_NGX_Handle* handle,NVSDK_NGX_Parameter* params,const UpscaleFrameInputs& f) {
    auto color=ngxResource(d,f.Color,false),depth=ngxResource(d,f.Depth,false),motion=ngxResource(d,f.MotionVectors,false),out=ngxResource(d,f.Output,true);
    NVSDK_NGX_VK_DLSS_Eval_Params p{};p.Feature.pInColor=&color;p.Feature.pInOutput=&out;
    p.pInDepth=&depth;p.pInMotionVectors=&motion;
    p.InJitterOffsetX=f.Jitter.x;p.InJitterOffsetY=f.Jitter.y;
    p.InMVScaleX=-float(f.RenderSize.x);p.InMVScaleY=-float(f.RenderSize.y);
    p.InReset=f.Reset;p.InPreExposure=f.PreExposure;p.InExposureScale=1;
    p.InRenderSubrectDimensions={uint32_t(f.RenderSize.x),uint32_t(f.RenderSize.y)};
    return NGX_VULKAN_EVALUATE_DLSS_EXT(*d.getCurrentFrame().mCommandBuffer,handle,params,&p);
}
#include "../dlssUpscalerCommon.h"
#undef NGX_CALL
#endif
}
#endif

std::unique_ptr<IKIGAI::RENDER::UpscalerInterface> IKIGAI::RENDER::DriverVk::createUpscaler(UpscalerType type) {
#ifdef IKIGAI_USE_STREAMLINE
    if(type==UpscalerType::DLSS){try{return CreateStreamlineUpscaler(std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime),streamlineCallbacks());}catch(const UnsupportedFrameGeneration& e){throw UnsupportedUpscaler(e.what());}}
#endif

#if defined(_WIN32) && defined(IKIGAI_USE_FSR)
    if(type==UpscalerType::FSR)return std::make_unique<FsrUpscaler>(*this);
#endif
#if defined(_WIN32) && defined(IKIGAI_USE_DLSS) && !defined(IKIGAI_USE_STREAMLINE)
    if(type==UpscalerType::DLSS) {
        if(!mDlssUnavailable.empty())throw UnsupportedUpscaler(mDlssUnavailable);
        return std::make_unique<DlssUpscaler>(*this);
    }
#endif
    throw UnsupportedUpscaler("Requested upscaler SDK is not compiled for Vulkan");
}
void IKIGAI::RENDER::DriverVk::configureUpscalerExtensions(std::vector<const char*>& extensions) {
#if defined(_WIN32) && (defined(IKIGAI_USE_FSR)||defined(IKIGAI_USE_DLSS)||defined(IKIGAI_USE_FSR_FRAME_GENERATION))
    VkPhysicalDeviceVulkan12Features support{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&support;
    vkGetPhysicalDeviceFeatures2(*mPhysicalDevice,&features);
    mRTCoreFeatures.shaderFloat16=support.shaderFloat16;
    mRTCoreFeatures.shaderSubgroupExtendedTypes=support.shaderSubgroupExtendedTypes;
#ifdef IKIGAI_USE_FSR_FRAME_GENERATION
    mRTCoreFeatures.timelineSemaphore=support.timelineSemaphore;
    mRTCoreFeatures.shaderBufferInt64Atomics=support.shaderBufferInt64Atomics;
    mRTCoreFeatures.shaderSharedInt64Atomics=support.shaderSharedInt64Atomics;
#endif
#if defined(IKIGAI_USE_FSR)||defined(IKIGAI_USE_FSR_FRAME_GENERATION)
    const auto availableFsr=mPhysicalDevice.enumerateDeviceExtensionProperties();
    // The signed legacy runtime queries KHR aliases even on Vulkan 1.3 devices.
    for(const char* name:{VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
                         VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME,VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME}) {
        if(std::any_of(availableFsr.begin(),availableFsr.end(),[name](auto& p){return std::string_view(p.extensionName)==name;})&&
           std::none_of(extensions.begin(),extensions.end(),[name](const char* p){return std::string_view(p)==name;}))extensions.push_back(name);
    }
#endif
#endif
#if defined(_WIN32) && defined(IKIGAI_USE_DLSS) && !defined(IKIGAI_USE_STREAMLINE)
    auto discovery=ngxDiscovery();uint32_t count=0;VkExtensionProperties* names=nullptr;
    if(NVSDK_NGX_FAILED(NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(*mInstance,*mPhysicalDevice,&discovery,&count,&names))) {
        mDlssUnavailable="NGX device extension query failed";return;
    }
    auto available=mPhysicalDevice.enumerateDeviceExtensionProperties();
    for(unsigned i=0;i<count;++i) {
        const auto name=names[i].extensionName;
        if(std::none_of(available.begin(),available.end(),[name](auto& p){return std::string_view(p.extensionName)==name;})) {
            mDlssUnavailable=std::string("DLSS requires Vulkan extension ")+name;continue;
        }
        if(std::none_of(extensions.begin(),extensions.end(),[name](const char* s){return std::string_view(s)==name;}))extensions.push_back(name);
    }
#endif
}
void IKIGAI::RENDER::DriverVk::configureUpscalerInstanceExtensions(std::vector<const char*>& extensions) {
    configureStreamlineExtensions(extensions,true);
#if defined(_WIN32) && defined(IKIGAI_USE_DLSS) && !defined(IKIGAI_USE_STREAMLINE)
    auto discovery=ngxDiscovery();uint32_t count=0;VkExtensionProperties* names=nullptr;
    if(NVSDK_NGX_FAILED(NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&discovery,&count,&names))) {
        mDlssUnavailable="NGX instance extension query failed";return;
    }
    auto available=mContext.enumerateInstanceExtensionProperties();
    for(unsigned i=0;i<count;++i) {
        const auto name=names[i].extensionName;
        if(std::none_of(available.begin(),available.end(),[name](auto& p){return std::string_view(p.extensionName)==name;})) {
            mDlssUnavailable=std::string("DLSS requires Vulkan instance extension ")+name;continue;
        }
        if(std::none_of(extensions.begin(),extensions.end(),[name](const char* s){return std::string_view(s)==name;}))extensions.push_back(name);
    }
#endif
}
#endif
