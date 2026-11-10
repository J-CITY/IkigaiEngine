#include "driverVk.h"
#ifdef VULKAN_BACKEND
#include "textureVk.h"
#include <iostream>
#ifdef IKIGAI_USE_STREAMLINE
#include "../streamlineRuntime.h"
#include <sl_helpers_vk.h>
namespace IKIGAI::RENDER {
void DriverVk::configureStreamlineExtensions(std::vector<const char*>& extensions,bool instance){
    if(!mDlssRuntime)return;
    auto runtime=std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime);
    for(const auto feature:{sl::kFeatureDLSS,sl::kFeatureDLSS_G,sl::kFeatureReflex}){
        sl::FeatureRequirements req{};
        if(runtime->requirements(feature,req)!=sl::Result::eOk)continue;
        const auto count=instance?req.vkNumInstanceExtensions:req.vkNumDeviceExtensions;
        auto names=instance?req.vkInstanceExtensions:req.vkDeviceExtensions;
        for(unsigned i=0;i<count;++i){
            if(!instance&&std::string_view(names[i])==VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME){
                if(std::none_of(extensions.begin(),extensions.end(),[](auto n){return std::string_view(n)==VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;}))extensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
                continue;
            }
            bool available=false;
            if(instance){for(const auto& e:mContext.enumerateInstanceExtensionProperties())if(std::string_view(e.extensionName)==names[i])available=true;}
            else {for(const auto& e:mPhysicalDevice.enumerateDeviceExtensionProperties())if(std::string_view(e.extensionName)==names[i])available=true;}
            if(available&&std::none_of(extensions.begin(),extensions.end(),[&](auto n){return std::string_view(n)==names[i];}))extensions.push_back(names[i]);
            if(!available){runtime->FGReason=std::string("Required Vulkan extension unavailable: ")+names[i];}
        }
        if(!instance){
            // Enable only the requested, physically supported feature bits.
            auto requested=sl::getVkPhysicalDeviceVulkan12Features(req.vkNumFeatures12,req.vkFeatures12);
            VkPhysicalDeviceVulkan12Features support{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
            VkPhysicalDeviceFeatures2 info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};info.pNext=&support;
            vkGetPhysicalDeviceFeatures2(*mPhysicalDevice,&info);
            auto* destination=&mRTCoreFeatures.samplerMirrorClampToEdge;
            auto* wanted=&requested.samplerMirrorClampToEdge;auto* possible=&support.samplerMirrorClampToEdge;
            constexpr auto fields=(offsetof(VkPhysicalDeviceVulkan12Features,subgroupBroadcastDynamicId)-offsetof(VkPhysicalDeviceVulkan12Features,samplerMirrorClampToEdge))/sizeof(VkBool32)+1;
            for(size_t i=0;i<fields;++i)if(wanted[i]&&possible[i])destination[i]=VK_TRUE;
        }
    }
}
void DriverVk::bindStreamlineDevice(){
    if(!mDlssRuntime)return;
    auto runtime=std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime);
    sl::VulkanInfo info{};info.device=*mDevice;info.physicalDevice=*mPhysicalDevice;info.instance=*mInstance;
    info.graphicsQueueFamily=mQueueFamilyIndex;info.graphicsQueueIndex=mPresentationQueues[0]?1:0;
    info.computeQueueFamily=mQueueFamilyIndex;info.computeQueueIndex=mPresentationQueues[1]?2:0;
    info.useNativeOpticalFlowMode=mOpticalFlowFamily!=UINT32_MAX;
    info.opticalFlowQueueFamily=info.useNativeOpticalFlowMode?mOpticalFlowFamily:0;info.opticalFlowQueueIndex=0;
    StreamlineRuntime::check(runtime->symbol<PFun_slSetVulkanInfo*>("slSetVulkanInfo")(info),"slSetVulkanInfo");
    std::cout<<"STREAMLINE_VULKAN_OPTICAL_FLOW native="<<info.useNativeOpticalFlowMode<<" family="<<info.opticalFlowQueueFamily<<'\n';
    sl::AdapterInfo adapter{};adapter.vkPhysicalDevice=reinterpret_cast<void*>(VkPhysicalDevice(*mPhysicalDevice));runtime->deviceReady(adapter);
}
StreamlineCallbacks DriverVk::streamlineCallbacks(){
	return { [this](const std::shared_ptr<TextureInterface>& texture, bool write) {
				deactivateRenderPass();
				auto t = std::dynamic_pointer_cast<TextureVk>(texture);
				if (!t)
					throw UnsupportedFrameGeneration("Streamline requires Vulkan textures");
				const auto layout = vk::ImageLayout::eGeneral;
				t->setState(getCurrentFrame().mCommandBuffer, layout);
				mRayTracingFrameResources.push_back(t);
				sl::Resource r { sl::ResourceType::eTex2d, reinterpret_cast<void*>(VkImage(t->mImagePtr)), nullptr,
					reinterpret_cast<void*>(VkImageView(*t->mImageView)), uint32_t(layout) };
				r.width = texture->getWidth();
				r.height = texture->getHeight();
				r.mipLevels = texture->getMipCount();
				r.arrayLayers = 1;
				r.flags = 0;
				switch (texture->getFormat()) {
				case PixelFormat::RGBA_FLOAT:
					r.nativeFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
					break;
				case PixelFormat::RGBA_INT:
					r.nativeFormat = VK_FORMAT_R8G8B8A8_UNORM;
					break;
				case PixelFormat::RG_FLOAT:
					r.nativeFormat = VK_FORMAT_R32G32_SFLOAT;
					break;
				case PixelFormat::DEPTH32_FLOAT:
					r.nativeFormat = VK_FORMAT_D32_SFLOAT;
					break;
				default:throw UnsupportedFrameGeneration("Unsupported Streamline Vulkan resource format");
				}
				// Match TextureVk::init: SDK cloning/barriers need the complete
				// native description, including copy and attachment usage.
				r.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
				r.usage |= texture->getType() == TextureType::DEPTH ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
																	: VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
				return r;
			},
		[this]() {
			invalidateCommandState();
			return reinterpret_cast<void*>(VkCommandBuffer(*getCurrentFrame().mCommandBuffer));
		},
		[this]() { invalidateCommandState(); }, [this](bool enabled) { streamlinePresenter(enabled); },
		[this](void* semaphore, uint64_t value) {
            if(!semaphore||!value)
				return;
            const VkSemaphore waitSemaphore=reinterpret_cast<VkSemaphore>(semaphore);
            VkSemaphoreWaitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
			info.semaphoreCount = 1;
			info.pSemaphores = &waitSemaphore;
			info.pValues = &value;
			if (isDeviceLost()) {
				requireUsableDevice();
			}
			checkFrameResult(vkWaitSemaphores(*mDevice, &info, GpuWaitTimeoutNs), "vkWaitSemaphores (Streamline)", true);
		} };
}
void DriverVk::streamlinePresenter(bool enable,bool commandOpen){
    auto runtime=std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime);
    if(!runtime)throw UnsupportedFrameGeneration("Streamline runtime unavailable");
    auto& ctx=getCurrentSwapchainContext();
    if(enable&&ctx.streamline&&ctx.sdkSwapchain)return;
    if(mShuttingDown)return;
	if (commandOpen) {
		end();
		wait();
	}
	waitDeviceIdle();
	runtime->clearTags();
	mCurrentState.mFrameBuffer.reset();
    for(auto& f:ctx.frames)f.mFrameBuffer.reset();for(auto& f:ctx.frames)f.destroyDeferred.clear();ctx.frames.clear();
    if(ctx.sdkSwapchain){
        auto get=runtime->symbol<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
        reinterpret_cast<PFN_vkDestroySwapchainKHR>(get(*mDevice,"vkDestroySwapchainKHR"))(*mDevice,ctx.sdkSwapchain,nullptr);
        ctx.sdkSwapchain=VK_NULL_HANDLE;ctx.sdkAcquire={};ctx.sdkPresent={};ctx.sdkResize={};
        ctx.sdkDestroy={};ctx.streamline=false;
    }
    ctx.swapchain=nullptr;
    if(!enable){createSwapchain(mCurrentWindowID,ctx.width,ctx.height);if(commandOpen){nextFrame();begin();}return;}
    auto info=static_cast<VkSwapchainCreateInfoKHR>(ctx.lastCreateInfo);info.oldSwapchain=VK_NULL_HANDLE;
    const auto capabilities=mPhysicalDevice.getSurfaceCapabilitiesKHR(*ctx.surface);
    if(!(capabilities.supportedUsageFlags&vk::ImageUsageFlagBits::eTransferDst))throw UnsupportedFrameGeneration("DLSS FG requires transfer-destination swapchain images");
    info.imageUsage|=VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if(mFrameGenerationSettings.vsync)throw UnsupportedFrameGeneration("DLSS Vulkan FG does not support VSync");
    const auto modes=mPhysicalDevice.getSurfacePresentModesKHR(*ctx.surface);
    if(std::find(modes.begin(),modes.end(),vk::PresentModeKHR::eImmediate)!=modes.end())info.presentMode=VK_PRESENT_MODE_IMMEDIATE_KHR;
    else if(std::find(modes.begin(),modes.end(),vk::PresentModeKHR::eMailbox)!=modes.end())info.presentMode=VK_PRESENT_MODE_MAILBOX_KHR;
    else throw UnsupportedFrameGeneration("Vulkan has no non-VSync present mode");
    const auto get=runtime->symbol<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
    auto create=reinterpret_cast<PFN_vkCreateSwapchainKHR>(get(*mDevice,"vkCreateSwapchainKHR"));
    const auto code=create(*mDevice,&info,nullptr,&ctx.sdkSwapchain);
    if(code!=VK_SUCCESS)throw UnsupportedFrameGeneration("Streamline Vulkan swapchain creation failed: "+std::to_string(code));
    auto acquire=reinterpret_cast<PFN_vkAcquireNextImageKHR>(get(*mDevice,"vkAcquireNextImageKHR"));
    auto present=reinterpret_cast<PFN_vkQueuePresentKHR>(get(*mDevice,"vkQueuePresentKHR"));
    auto images=reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(get(*mDevice,"vkGetSwapchainImagesKHR"));
	ctx.sdkAcquire = [this, acquire](VkSemaphore semaphore, uint32_t* index) {
		return acquire(*mDevice, getCurrentSwapchainContext().sdkSwapchain, GpuWaitTimeoutNs, semaphore, VK_NULL_HANDLE, index);
	};
	ctx.sdkPresent = [this, present](const VkPresentInfoKHR* info) { return present(*mQueue, info); };
	ctx.streamline = true;
	ctx.sdkDestroy=[this,get](){
        auto& c=getCurrentSwapchainContext();
        if(c.sdkSwapchain)reinterpret_cast<PFN_vkDestroySwapchainKHR>(get(*mDevice,"vkDestroySwapchainKHR"))(*mDevice,c.sdkSwapchain,nullptr);
        c.sdkSwapchain=VK_NULL_HANDLE;c.streamline=false;
    };
    ctx.sdkResize=[this,runtime,get](uint32_t w,uint32_t h){
        auto& c=getCurrentSwapchainContext();runtime->clearTags();
        mCurrentState.mFrameBuffer.reset();
        for(auto& f:c.frames)f.mFrameBuffer.reset();for(auto& f:c.frames)f.destroyDeferred.clear();c.frames.clear();
        auto destroy=reinterpret_cast<PFN_vkDestroySwapchainKHR>(get(*mDevice,"vkDestroySwapchainKHR"));destroy(*mDevice,c.sdkSwapchain,nullptr);
        c.sdkSwapchain=VK_NULL_HANDLE;c.sdkAcquire={};c.sdkPresent={};c.sdkResize={};c.sdkDestroy={};c.streamline=false;
        createSwapchain(mCurrentWindowID,w,h);
        streamlinePresenter(true,false);
    };
    uint32_t count=0;images(*mDevice,ctx.sdkSwapchain,&count,nullptr);std::vector<vk::Image> buffers(count);
    images(*mDevice,ctx.sdkSwapchain,&count,reinterpret_cast<VkImage*>(buffers.data()));createSwapchainFrames(mCurrentWindowID,buffers);
    if(commandOpen){nextFrame();begin();}
}
void DriverVk::setTemporalCamera(const FrameGenerationInputs& f){if(mDlssRuntime)std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->camera(f);}
void DriverVk::latencyFrameBegin(){DriverInterface::latencyFrameBegin();if(mDlssRuntime)std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->beginFrame(uint32_t(mRealFrameId));}
void DriverVk::latencySimulationEnd(){if(mDlssRuntime)std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->marker(sl::PCLMarker::eSimulationEnd);}
void DriverVk::latencySubmitBegin(){if(mDlssRuntime)std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->marker(sl::PCLMarker::eRenderSubmitStart);}
void DriverVk::latencySubmitEnd(){if(mDlssRuntime)std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->marker(sl::PCLMarker::eRenderSubmitEnd);}
}
#else
namespace IKIGAI::RENDER {
void DriverVk::configureStreamlineExtensions(std::vector<const char*>&,bool){}
void DriverVk::bindStreamlineDevice(){}
void DriverVk::setTemporalCamera(const FrameGenerationInputs&){}
void DriverVk::latencyFrameBegin(){DriverInterface::latencyFrameBegin();}
void DriverVk::latencySimulationEnd(){}
void DriverVk::latencySubmitBegin(){}
void DriverVk::latencySubmitEnd(){}
}
#endif
#endif
