#include "driverVk.h"
#ifdef VULKAN_BACKEND
#include "textureVk.h"
#include "frameBufferVk.h"
#include <utilsModule/imguiHelper/imguiBackend/imguiBackend.h>
#include <iostream>
#ifdef IKIGAI_USE_STREAMLINE
#include "../streamlineRuntime.h"
#endif
#ifdef IKIGAI_USE_FSR_FRAME_GENERATION
#include "../upscalerSdkUtils.h"
#include <ffx_api/ffx_framegeneration.h>
#include <ffx_api/vk/ffx_api_vk.h>

namespace IKIGAI::RENDER {
class FsrFrameGenerationVk final : public FrameGenerationInterface {
    DriverVk& d;
    UpscalerModule module{L"amd_fidelityfx_vk.dll"};
    PfnFfxCreateContext create=module.function<PfnFfxCreateContext>("ffxCreateContext");
    PfnFfxDestroyContext destroy=module.function<PfnFfxDestroyContext>("ffxDestroyContext");
    PfnFfxConfigure configureApi=module.function<PfnFfxConfigure>("ffxConfigure");
    PfnFfxDispatch dispatch=module.function<PfnFfxDispatch>("ffxDispatch");
    PfnFfxQuery query=module.function<PfnFfxQuery>("ffxQuery");
    ffxContext fg{},swap{};
    ffxQueryDescSwapchainReplacementFunctionsVK functions{};
    FrameGenerationStatus state;
    MATH::Vector2i renderSize{},displaySize{};
    FrameGenerationInputs last;
    bool attached=false,prepared=false,vsync=false;
    uint64_t observedPresents=0;
    static void check(ffxReturnCode_t code,const char* call){
        if(code)throw UnsupportedFrameGeneration(std::string(call)+": "+std::to_string(code));
    }
    void clearFrames(){
        auto& ctx=d.getCurrentSwapchainContext();d.mCurrentState.mFrameBuffer.reset();
        for(auto& f:ctx.frames)f.mFrameBuffer.reset();
        for(auto& f:ctx.frames)f.destroyDeferred.clear();ctx.frames.clear();
    }
    void attach(bool commandOpen){
        if(!d.mPresentationQueues[0]||!d.mPresentationQueues[1]||!d.mPresentationQueues[2])
            throw UnsupportedFrameGeneration("FSR Vulkan FG needs four graphics-capable queues");
		if (commandOpen) {
			d.end();
			d.wait();
			d.waitDeviceIdle();
		}
		auto& ctx=d.getCurrentSwapchainContext();
        auto info=static_cast<VkSwapchainCreateInfoKHR>(ctx.lastCreateInfo);
        if(vsync)info.presentMode=VK_PRESENT_MODE_FIFO_KHR;
        else{
            const auto modes=d.mPhysicalDevice.getSurfacePresentModesKHR(*ctx.surface);
            if(std::find(modes.begin(),modes.end(),vk::PresentModeKHR::eImmediate)!=modes.end())info.presentMode=VK_PRESENT_MODE_IMMEDIATE_KHR;
            else if(std::find(modes.begin(),modes.end(),vk::PresentModeKHR::eMailbox)!=modes.end())info.presentMode=VK_PRESENT_MODE_MAILBOX_KHR;
        }
        auto raw=VkSwapchainKHR(ctx.swapchain.release());info.oldSwapchain=raw;
        clearFrames();
        ffxCreateContextDescFrameGenerationSwapChainVK desc{};desc.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK;
        desc.physicalDevice=*d.mPhysicalDevice;desc.device=*d.mDevice;desc.swapchain=&raw;desc.createInfo=info;
        desc.gameQueue={*d.mQueue,d.mQueueFamilyIndex,nullptr};
        desc.asyncComputeQueue={d.mPresentationQueues[0],d.mQueueFamilyIndex,nullptr};
        desc.presentQueue={d.mPresentationQueues[1],d.mQueueFamilyIndex,nullptr};
        desc.imageAcquireQueue={d.mPresentationQueues[2],d.mQueueFamilyIndex,nullptr};
        const auto code=create(&swap,&desc.header,nullptr);
        if(code){
            // The SDK may already have consumed the original native swapchain.
            // Recreate a native one instead of adopting a possibly stale handle.
            if(swap){destroy(&swap,nullptr);swap=nullptr;}
            d.createSwapchain(d.mCurrentWindowID,ctx.width,ctx.height);
            if(commandOpen){d.nextFrame();d.begin();}
            check(code,"FSR FG create Vulkan swapchain");
        }
        attached=true;ctx.sdkSwapchain=raw;observedPresents=0;
        functions.header.type=FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_FUNCTIONS_VK;
        check(query(&swap,&functions.header),"FSR FG Vulkan replacement functions");
        ctx.sdkAcquire=[this](VkSemaphore semaphore,uint32_t* image){return functions.pOutAcquireNextImageKHR(*d.mDevice,d.getCurrentSwapchainContext().sdkSwapchain,UINT64_MAX,semaphore,VK_NULL_HANDLE,image);};
        ctx.sdkPresent=[this](const VkPresentInfoKHR* info){return functions.pOutQueuePresentKHR(*d.mQueue,info);};
        ctx.sdkResize=[this](uint32_t w,uint32_t h){
            releaseFG();releaseSwap();d.createSwapchain(d.mCurrentWindowID,w,h);attach(false);
        };
        uint32_t count=0;check(functions.pOutGetSwapchainImagesKHR(*d.mDevice,raw,&count,nullptr),"FSR FG Vulkan image count");
        std::vector<vk::Image> images(count);
        check(functions.pOutGetSwapchainImagesKHR(*d.mDevice,raw,&count,reinterpret_cast<VkImage*>(images.data())),"FSR FG Vulkan images");
        d.createSwapchainFrames(d.mCurrentWindowID,images);
        if(commandOpen){d.nextFrame();d.begin();}
    }
    void releaseFG(){if(fg){destroy(&fg,nullptr);fg=nullptr;}last={};prepared=false;}
    void releaseSwap(){
        if(!attached)return;
        clearFrames();auto& ctx=d.getCurrentSwapchainContext();
        ctx.sdkAcquire={};ctx.sdkPresent={};ctx.sdkResize={};ctx.sdkSwapchain=VK_NULL_HANDLE;
        if(swap){destroy(&swap,nullptr);swap=nullptr;}attached=false;
    }
public:
    explicit FsrFrameGenerationVk(DriverVk& driver):d(driver){
        state.Technology=FrameGenerationType::FSR;state.MaxMultiplier=2;state.Multiplier=2;
    }
    ~FsrFrameGenerationVk() override {
        try{
			if (attached && !d.mShuttingDown && !d.isDeviceLost()) {
				try {
					d.end();
					d.wait();
				} catch (...) {
					if (!d.isDeviceLost()) {
						throw;
					}
				}
			}
			if (!d.isDeviceLost()) {
				if (fg) {
					suspend();
				}
				if (swap) {
					waitForPresents();
				}
			}
			releaseFG();
			if(attached){
                auto& ctx=d.getCurrentSwapchainContext();const auto w=ctx.width,h=ctx.height;
                releaseSwap();
				if (!d.mShuttingDown && !d.isDeviceLost()) {
					d.createSwapchain(d.mCurrentWindowID, w, h);
					d.nextFrame();
					d.begin();
				}
			}
        }catch(const std::exception& error){std::cerr<<"FSR Vulkan FG teardown: "<<error.what()<<'\n';}
    }
    void configure(const FrameGenerationSettings& settings,MATH::Vector2i render,MATH::Vector2i display) override {
        if(settings.multiplier!=2)throw UnsupportedFrameGeneration("FSR FG supports x2 in this SDK");
        vsync=settings.vsync;
        if(fg&&render==renderSize&&display==displaySize)return;
        if(fg){suspend();waitForPresents();releaseFG();}
        ffxCreateBackendVKDesc backend{};backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
        backend.vkDevice=*d.mDevice;backend.vkPhysicalDevice=*d.mPhysicalDevice;backend.vkDeviceProcAddr=vkGetDeviceProcAddr;
        ffxCreateContextDescFrameGenerationHudless hudless{};
        hudless.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_HUDLESS,&backend.header};
        hudless.hudlessBackBufferFormat=FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
        ffxCreateContextDescFrameGeneration desc{};desc.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION,&hudless.header};
        desc.displaySize={uint32_t(display.x),uint32_t(display.y)};desc.maxRenderSize={uint32_t(render.x),uint32_t(render.y)};
        desc.backBufferFormat=ffxApiGetSurfaceFormatVK(static_cast<VkFormat>(d.mSurfaceFormat.format));
        check(create(&fg,&desc.header,nullptr),"FSR FG create Vulkan context");
        if(!attached)attach(true);
        ffxQueryGetProviderVersion v{};v.header.type=FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        if(!query(&fg,&v.header)&&v.versionName)state.Implementation=v.versionName;
        renderSize=render;displaySize=display;state.Supported=true;
        std::cout<<"FRAMEGEN_CONTEXT FSR Vulkan "<<state.Implementation<<" "<<display.x<<"x"<<display.y<<'\n';
    }
    FfxApiResource resource(const std::shared_ptr<TextureInterface>& texture){
        auto t=std::dynamic_pointer_cast<TextureVk>(texture);
        if(!t)throw UnsupportedFrameGeneration("FSR FG requires Vulkan textures");
        // Final scene/UI composition samples these textures in GENERAL after
        // this stage. Declare their layout at SDK execution, then let FFX
        // transition and restore them around its own shader reads.
        t->setState(d.getCurrentFrame().mCommandBuffer,vk::ImageLayout::eGeneral);
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType=VK_IMAGE_TYPE_2D;
        switch(texture->getFormat()){
        case PixelFormat::RGBA_INT:info.format=VK_FORMAT_R8G8B8A8_UNORM;break;
        case PixelFormat::RG_FLOAT:info.format=VK_FORMAT_R32G32_SFLOAT;break;
        case PixelFormat::DEPTH32_FLOAT:info.format=VK_FORMAT_D32_SFLOAT;break;
        default:throw UnsupportedFrameGeneration("Unsupported FSR FG Vulkan input format");}
        info.extent={uint32_t(texture->getWidth()),uint32_t(texture->getHeight()),1};info.mipLevels=1;info.arrayLayers=1;info.usage=VK_IMAGE_USAGE_SAMPLED_BIT;
        return ffxApiGetResourceVK(reinterpret_cast<void*>(VkImage(t->mImagePtr)),ffxApiGetImageResourceDescriptionVK(t->mImagePtr,info,0),FFX_API_RESOURCE_STATE_COMMON);
    }
    void prepare(const FrameGenerationInputs& f) override {
        d.deactivateRenderPass();last=f;
        ffxConfigureDescFrameGeneration config{};config.header.type=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        config.swapChain=reinterpret_cast<void*>(d.getCurrentSwapchainContext().sdkSwapchain);
        config.frameGenerationEnabled=true;config.allowAsyncWorkloads=false;config.frameID=f.RealFrameId;
        config.HUDLessColor=resource(f.Color);
        config.generationRect={0,0,f.DisplaySize.x,f.DisplaySize.y};
        check(configureApi(&fg,&config.header),"FSR Vulkan FG configure");
        ffxConfigureDescFrameGenerationSwapChainRegisterUiResourceVK ui{};
        ui.header.type=FFX_API_CONFIGURE_DESC_TYPE_FGSWAPCHAIN_REGISTERUIRESOURCE_VK;
        ui.uiResource=resource(f.UI);ui.flags=FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_USE_PREMUL_ALPHA|FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_ENABLE_INTERNAL_UI_DOUBLE_BUFFERING;
        check(configureApi(&swap,&ui.header),"FSR Vulkan FG register UI");
        ffxDispatchDescFrameGenerationPrepare prep{};prep.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
        prep.frameID=f.RealFrameId;prep.commandList=reinterpret_cast<void*>(VkCommandBuffer(*d.getCurrentFrame().mCommandBuffer));
        prep.renderSize={uint32_t(f.RenderSize.x),uint32_t(f.RenderSize.y)};prep.jitterOffset={f.Jitter.x,f.Jitter.y};
        prep.motionVectorScale={-float(f.RenderSize.x),-float(f.RenderSize.y)};
        prep.frameTimeDelta=f.DeltaMilliseconds;prep.cameraNear=f.Near;prep.cameraFar=f.Far;prep.cameraFovAngleVertical=f.VerticalFov;prep.viewSpaceToMetersFactor=1;
        prep.depth=resource(f.Depth);prep.motionVectors=resource(f.MotionVectors);
        check(dispatch(&fg,&prep.header),"FSR Vulkan FG prepare");
        void* commands=nullptr;FfxApiResource output{};
        ffxQueryDescFrameGenerationSwapChainInterpolationCommandListVK cmd{};
        cmd.header.type=FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_INTERPOLATIONCOMMANDLIST_VK;cmd.pOutCommandList=&commands;
        check(query(&swap,&cmd.header),"FSR Vulkan FG interpolation commands");
        ffxQueryDescFrameGenerationSwapChainInterpolationTextureVK tex{};
        tex.header.type=FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_INTERPOLATIONTEXTURE_VK;tex.pOutTexture=&output;
        check(query(&swap,&tex.header),"FSR Vulkan FG interpolation output");
        ffxDispatchDescFrameGeneration gen{};gen.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
        auto backbuffer=std::static_pointer_cast<TextureVk>(d.getCurrentFrame().mFrameBuffer->getTextures()[0]);
        VkImageCreateInfo presentInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};presentInfo.imageType=VK_IMAGE_TYPE_2D;
        presentInfo.format=static_cast<VkFormat>(d.mSurfaceFormat.format);presentInfo.extent={uint32_t(f.DisplaySize.x),uint32_t(f.DisplaySize.y),1};
        presentInfo.mipLevels=1;presentInfo.arrayLayers=1;presentInfo.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        // The host will copy the real scene and transition this virtual image to shader read
        // before the swapchain executes the interpolation command buffer.
        gen.commandList=commands;gen.presentColor=ffxApiGetResourceVK(reinterpret_cast<void*>(VkImage(backbuffer->mImagePtr)),
            ffxApiGetImageResourceDescriptionVK(backbuffer->mImagePtr,presentInfo,0),FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        gen.outputs[0]=output;gen.numGeneratedFrames=1;
        gen.reset=f.Reset;gen.backbufferTransferFunction=FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
        gen.minMaxLuminance[0]=0;gen.minMaxLuminance[1]=1;gen.frameID=f.RealFrameId;gen.generationRect=config.generationRect;
        check(dispatch(&fg,&gen.header),"FSR Vulkan FG generate");
		d.mBoundPipelineKey.reset();
		d.mDirty.set();
		state.Active = true;
		prepared = true;
		if(state.RealFrames==0||f.Reset)std::cout<<"FRAMEGEN_PREPARE FSR frame="<<f.RealFrameId<<" reset="<<f.Reset<<'\n';
    }
    void suspend() override {
        if(fg){ffxConfigureDescFrameGeneration config{};config.header.type=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
            config.swapChain=reinterpret_cast<void*>(d.getCurrentSwapchainContext().sdkSwapchain);configureApi(&fg,&config.header);}
        prepared=false;state.Active=false;
    }
    void waitForPresents() override {
        if(swap){ffxDispatchDescFrameGenerationSwapChainWaitForPresentsVK wait{};wait.header.type=FFX_API_DISPATCH_DESC_TYPE_FGSWAPCHAIN_WAIT_FOR_PRESENTS_VK;
            check(dispatch(&swap,&wait.header),"FSR Vulkan FG wait for presents");}
    }
    void afterPresent() override {
        if(prepared){++state.RealFrames;
            const auto count=functions.pOutGetLastPresentCountFFXAPI(d.getCurrentSwapchainContext().sdkSwapchain);
            const auto batch=count>=observedPresents?count-observedPresents:count;
            observedPresents=count;state.PresentedFrames+=batch;state.GeneratedFrames+=batch>0?batch-1:0;
            std::cout<<"FRAMEGEN_PRESENT FSR real="<<state.RealFrames<<" generated="<<state.GeneratedFrames<<" presented="<<state.PresentedFrames<<'\n';
        }
        prepared=false;last={};
    }
    FrameGenerationStatus status() const override{return state;}
    bool composesUI() const override{return attached;}
};
}
#endif
std::unique_ptr<IKIGAI::RENDER::FrameGenerationInterface> IKIGAI::RENDER::DriverVk::createFrameGeneration(FrameGenerationType type){
#ifdef IKIGAI_USE_DLSS_FRAME_GENERATION
    // The pinned NGX FG Vulkan runtime emits internal transfer/clear sync
    // hazards even with native OFA and full input metadata. Do not expose the
    // crashing path as available; re-enable after updating and validating SDK.
    if(type==FrameGenerationType::DLSS)throw UnsupportedFrameGeneration("DLSS FG Vulkan is unavailable with Streamline 2.14.1: NGX internal synchronization errors; FSR FG remains available");
#endif
#ifdef IKIGAI_USE_FSR_FRAME_GENERATION
    if(type==FrameGenerationType::FSR){try{return std::make_unique<FsrFrameGenerationVk>(*this);}catch(const UnsupportedUpscaler& e){throw UnsupportedFrameGeneration(e.what());}}
#endif
    throw UnsupportedFrameGeneration("Requested Frame Generation SDK is not compiled for Vulkan");
}
void IKIGAI::RENDER::DriverVk::renderPresentationUI(std::shared_ptr<FrameBufferInterface> target){
    if(auto* imgui=IKIGAI::IMGUI::Get())imgui->renderDrawDataTo(std::move(target));
	mPresentationUIRendered = true;
	mBoundPipelineKey.reset();
	mDirty.set();
}
#endif
