

#include "driverVk.h"
//#include "Render/vk/raytracing/dw/include/macros.h"
#ifdef VULKAN_BACKEND
#include <SDL_vulkan.h>
#include <string>
#include <string_view>
#include <unordered_set>

#include "backends/imgui_impl_vulkan.h"
#include "renderModule/backends/interface/storageBufferInterface.h"
#include "utilsModule/log/loggerDefine.h"

//#include "raytracing/dw/include/extensions_vk.h"

#include <array>
#include <iostream>
#include <set>
#include <stdexcept>
#include <assimp/Importer.hpp>

#include <../../3rd/imgui/imgui/imgui.h>
#include "DebugMessanger.h"
#include "uniformBufferVk.h"
#include <assimp/postprocess.h>
#include "modelVk.h"
#include "textureVk.h"
#include "shaderVk.h"
#include "frameBufferVk.h"
#include "storageBufferVk.h"
#include "resourceModule/serviceManager.h"
#include "resourceModule/modelManager.h"
#include "resourceModule/materialManager.h"
#include "modelVk.h"
#include "materialVk.h"
//#include "../../GUI.h"


#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <windowModule/window/window.h>

using namespace IKIGAI;
using namespace IKIGAI::RENDER;

#if defined(DEBUG) || defined(_DEBUG) 
VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageTypes, VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData,
	void* /*pUserData*/) {
#if !defined( NDEBUG )
	if (pCallbackData->messageIdNumber == 648835635) {
		// UNASSIGNED-khronos-Validation-debug-build-warning-message
		return VK_FALSE;
	}
	if (pCallbackData->messageIdNumber == 767975156) {
		// UNASSIGNED-BestPractices-vkCreateInstance-specialuse-extension
		return VK_FALSE;
	}
#endif
	// Vulkan loader noise caused by third-party overlay layers installed on the machine
	// (GOG Galaxy / FPS Monitor / duplicate RenderDoc layer). Not an application problem.
	if (pCallbackData->pMessageIdName && std::string_view(pCallbackData->pMessageIdName) == "Loader Message" && pCallbackData->pMessage) {
		const std::string_view msg(pCallbackData->pMessage);
		if (msg.find("does not conform to naming standard") != std::string_view::npos ||
			msg.find("because it is a duplicate of") != std::string_view::npos) {
			return VK_FALSE;
		}
	}

	std::cerr << vk::to_string(static_cast<vk::DebugUtilsMessageSeverityFlagBitsEXT>(messageSeverity)) << ": "
		<< vk::to_string(static_cast<vk::DebugUtilsMessageTypeFlagsEXT>(messageTypes)) << ":\n";
	std::cerr << "\t" << "messageIdName   = <" << pCallbackData->pMessageIdName << ">\n";
	std::cerr << "\t" << "messageIdNumber = " << pCallbackData->messageIdNumber << "\n";
	std::cerr << "\t" << "message         = <" << pCallbackData->pMessage << ">\n";
	if (pCallbackData->queueLabelCount > 0) {
		std::cerr << "\t" << "Queue Labels:\n";
		for (uint32_t i = 0; i < pCallbackData->queueLabelCount; i++) {
			std::cerr << "\t\t" << "labelName = <" << pCallbackData->pQueueLabels[i].pLabelName << ">\n";
		}
	}
	if (pCallbackData->cmdBufLabelCount > 0) {
		std::cerr << "\t" << "CommandBuffer Labels:\n";
		for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; i++) {
			std::cerr << "\t\t" << "labelName = <" << pCallbackData->pCmdBufLabels[i].pLabelName << ">\n";
		}
	}
	if (pCallbackData->objectCount > 0) {
		std::cerr << "\t" << "Objects:\n";
		for (uint32_t i = 0; i < pCallbackData->objectCount; i++) {
			std::cerr << "\t\t" << "Object " << i << "\n";
			std::cerr << "\t\t\t" << "objectType   = " << vk::to_string(static_cast<vk::ObjectType>(pCallbackData->pObjects[i].objectType))
				<< "\n";
			std::cerr << "\t\t\t" << "objectHandle = " << pCallbackData->pObjects[i].objectHandle << "\n";
			if (pCallbackData->pObjects[i].pObjectName) {
				std::cerr << "\t\t\t" << "objectName   = <" << pCallbackData->pObjects[i].pObjectName << ">\n";
			}
		}
	}

	if (vk::DebugUtilsMessageSeverityFlagBitsEXT::eError == static_cast<vk::DebugUtilsMessageSeverityFlagBitsEXT>(messageSeverity)) {
		int a = 0;
	}
	return VK_TRUE;
}
#endif


static const std::unordered_map<PixelFormat, vk::Format> FormatMap = {
	{PixelFormat::R_FLOAT, vk::Format::eR32Sfloat},
	{PixelFormat::RG_FLOAT, vk::Format::eR32G32Sfloat},
	{PixelFormat::RGB_FLOAT, vk::Format::eR32G32B32Sfloat},
	{PixelFormat::RGBA_FLOAT, vk::Format::eR32G32B32A32Sfloat},
	{PixelFormat::R_INT, vk::Format::eR8Unorm},
	{PixelFormat::RG_INT, vk::Format::eR8G8Unorm},
	{PixelFormat::RGB_INT, vk::Format::eR8G8B8Unorm},
	{PixelFormat::RGBA_INT, vk::Format::eR8G8B8A8Unorm},
	{PixelFormat::R32_INT, vk::Format::eR32Sint},
	{PixelFormat::RG32_INT, vk::Format::eR32G32Sint},
	{PixelFormat::RGB32_INT, vk::Format::eR32G32B32Sint},
	{PixelFormat::RGBA32_INT, vk::Format::eR32G32B32A32Sint},
	{PixelFormat::BGRA_INT, vk::Format::eB8G8R8A8Unorm},
	{PixelFormat::DEPTH_24_UNORM_STENCIL_8_UINT, vk::Format::eD24UnormS8Uint},
	{PixelFormat::DEPTH_32_FLOAT_STENCIL_8_UINT, vk::Format::eD32SfloatS8Uint},
};

std::string DriverVk::State::getName() {
	return std::to_string(mShader->getId()) + 
		(mFrameBuffer ? std::to_string(mFrameBuffer->getId()) : "0");
}

DriverVk::VolkInitializer::VolkInitializer() {
	if (volkInitialize() != VK_SUCCESS) {
		throw std::runtime_error("Failed to initialize volk");
	}
}

DriverVk::DriverVk() {
	UtilityVk::mDriver = this;
	DriverVk::init();
}

DriverVk::~DriverVk() {
	if (working) {
		DriverVk::end();
	}
	if (*mDevice) {
		mDevice.waitIdle();
	}
	wait();

	mShuttingDown = true;
	mVertexBuffer.reset();
	mIndexBuffer.reset();
	mUniformBuffers.clear();
	mStorageBuffers.clear();
	mTextures.clear();
	mStates.clear();
	for (auto& [_, ctx] : mSwapchains) {
		for (auto& frame : ctx.frames) {
			frame.destroyDeferred.clear();
		}
	}
	mSwapchains.clear();

#if defined(DEBUG) || defined(_DEBUG)
	if (mAllocator) {
		VmaTotalStatistics stats{};
		vmaCalculateStatistics(mAllocator, &stats);
		LOG_INFO << "VMA shutdown: allocations=" << stats.total.statistics.allocationCount
			<< " bytes=" << stats.total.statistics.allocationBytes;
	}
#endif
	if (mAllocator) {
		vmaDestroyAllocator(mAllocator);
		mAllocator = nullptr;
	}
}

void DriverVk::init() {
	//auto all_extensions = mContext.enumerateInstanceExtensionProperties();
	//
	//for (auto extension : all_extensions) {
	//	//	std::cout << extension.extensionName << std::endl;
	//}
	//
	//auto all_layers = mContext.enumerateInstanceLayerProperties();
	//
	//for (auto layer : all_layers) {
	//	//	std::cout << layer.layerName << std::endl;
	//}
	auto& win = RESOURCES::ServiceManager::Get<WINDOW::Window>();
#if defined(DEBUG) || defined(_DEBUG)
	std::vector validationLayers = {
		"VK_LAYER_KHRONOS_validation"
	};

	if (!checkValidationLayerSupport(validationLayers))
		throw std::runtime_error("VkInstance doesn't support the required validation layers");

#endif

//	auto extensions = {
//		VK_KHR_SURFACE_EXTENSION_NAME,
//#if defined(DEBUG) || defined(_DEBUG)
//		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
//#endif
//	};

	auto extensions = win.getSDLVulkanExtentions();
	extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#if defined(DEBUG) || defined(_DEBUG)
	extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
	if (!checkInstanceExtensionSupport(extensions))
		throw std::runtime_error("VkInstance doesn't support the required extensions");


	//auto version = mContext.enumerateInstanceVersion();
	//auto major_version = VK_API_VERSION_MAJOR(version);
	//auto minor_version = VK_API_VERSION_MINOR(version);
	//auto patch_version = VK_API_VERSION_PATCH(version);

	//std::cout << "available vulkan version: " << major_version << "." << minor_version << std::endl;

	vk::ApplicationInfo applicationInfo{};
	applicationInfo.setApiVersion(VK_API_VERSION_1_3); //VK_API_VERSION_1_4

	vk::InstanceCreateInfo instanceInfo{};
	instanceInfo.setPEnabledExtensionNames(extensions);
	instanceInfo.setPApplicationInfo(&applicationInfo);

#if defined(DEBUG) || defined(_DEBUG) 
	instanceInfo.setPEnabledLayerNames(validationLayers);
#endif

#if defined(DEBUG) || defined(_DEBUG) 
	vk::DebugUtilsMessengerCreateInfoEXT debugMessengerInfo{};
	debugMessengerInfo.setMessageSeverity(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
	debugMessengerInfo.setMessageType(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
	debugMessengerInfo.setPfnUserCallback(&DebugCallback);

	auto features = {
		//	vk::ValidationFeatureEnableEXT::eBestPractices,
		// DebugPrintf is disabled: shaders don't use debugPrintfEXT, and enabling it makes the validation layer
		// emit "DebugPrintf logs to the Information severity" and several "Adding Vk...Features to pNext" warnings.
		//	vk::ValidationFeatureEnableEXT::eDebugPrintf,
		//	vk::ValidationFeatureEnableEXT::eGpuAssisted,
		//	vk::ValidationFeatureEnableEXT::eGpuAssistedReserveBindingSlot,
		vk::ValidationFeatureEnableEXT::eSynchronizationValidation
	};

	vk::ValidationFeaturesEXT validationFeatures{};
	validationFeatures.setEnabledValidationFeatures(features);
#endif

	auto structureChain = vk::StructureChain<vk::InstanceCreateInfo
#if defined(DEBUG) || defined(_DEBUG) 
		, vk::DebugUtilsMessengerCreateInfoEXT, vk::ValidationFeaturesEXT
#endif
	>(instanceInfo
#if defined(DEBUG) || defined(_DEBUG) 
		, debugMessengerInfo, validationFeatures
#endif
		);

	mInstance = mContext.createInstance(structureChain.get<vk::InstanceCreateInfo>());
	volkLoadInstance(*mInstance);

#if defined(DEBUG) || defined(_DEBUG) 
	mDebugMessenger = mInstance.createDebugUtilsMessengerEXT(debugMessengerInfo);
#endif

	auto devices = mInstance.enumeratePhysicalDevices();
	size_t device_index = 0;
	auto preferred_device_type = vk::PhysicalDeviceType::eDiscreteGpu;//TODO: adapter == Adapter::HighPerformance ? vk::PhysicalDeviceType::eDiscreteGpu : vk::PhysicalDeviceType::eIntegratedGpu;
	for (size_t i = 0; i < devices.size(); i++) {
		auto properties = devices.at(i).getProperties();
		if (properties.deviceType == preferred_device_type) {
			device_index = i;
			break;
		}
	}

	mPhysicalDevice = std::move(devices.at(device_index));

	auto properties = mPhysicalDevice.getQueueFamilyProperties();

	for (size_t i = 0; i < properties.size(); i++) {
		if (properties[i].queueFlags & vk::QueueFlagBits::eGraphics) {
			mQueueFamilyIndex = static_cast<uint32_t>(i);
			break;
		}
	}

	auto all_device_extensions = mPhysicalDevice.enumerateDeviceExtensionProperties();

	for (auto device_extension : all_device_extensions) {
		//	std::cout << device_extension.extensionName << std::endl;
	}

	std::vector device_extensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		// dynamic pipeline
		VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
		VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
	};

	//if (features.contains(Feature::Raytracing)) {
	//	device_extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
	//	device_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	//	device_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	//}

	auto queuePriority = {1.0f};

	vk::DeviceQueueCreateInfo queueInfo{};
	queueInfo.setQueueFamilyIndex(mQueueFamilyIndex);
	queueInfo.setQueuePriorities(queuePriority);

	auto deviceFeatures = mPhysicalDevice.getFeatures2<
		vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT
	>();

	vk::DeviceCreateInfo deviceInfo{};
	deviceInfo.setQueueCreateInfos(queueInfo);
	deviceInfo.setPEnabledExtensionNames(device_extensions);

	//if (features.contains(Feature::Raytracing))
	//	deviceInfo.setPNext(&raytracing_device_features.get<vk::PhysicalDeviceFeatures2>());
	//else
	deviceInfo.setPNext(&deviceFeatures.get<vk::PhysicalDeviceFeatures2>());

	mDevice = mPhysicalDevice.createDevice(deviceInfo);
	volkLoadDevice(*mDevice);

	{
		VmaVulkanFunctions vulkanFunctions{};
		vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
		vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

		VmaAllocatorCreateInfo allocatorCreateInfo{};
		allocatorCreateInfo.physicalDevice = *mPhysicalDevice;
		allocatorCreateInfo.device = *mDevice;
		allocatorCreateInfo.instance = *mInstance;
		allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_3;
		allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;

		const VkResult allocatorResult = vmaCreateAllocator(&allocatorCreateInfo, &mAllocator);
		if (allocatorResult != VK_SUCCESS) {
			throw std::runtime_error("vmaCreateAllocator failed");
		}

#if defined(DEBUG) || defined(_DEBUG)
		VmaTotalStatistics stats{};
		vmaCalculateStatistics(mAllocator, &stats);
		LOG_INFO << "VMA init: allocations=" << stats.total.statistics.allocationCount
			<< " bytes=" << stats.total.statistics.allocationBytes;
#endif
	}

	mQueue = mDevice.getQueue(mQueueFamilyIndex, 0);

//#if defined(SKYGFX_PLATFORM_WINDOWS)
//	auto surface_info = vk::Win32SurfaceCreateInfoKHR()
//		.setHwnd((HWND)window);
//#elif defined(SKYGFX_PLATFORM_MACOS)
//	auto surface_info = vk::MacOSSurfaceCreateInfoMVK()
//		.setPView(window);
//#elif defined(SKYGFX_PLATFORM_IOS)
//	auto surface_info = vk::IOSSurfaceCreateInfoMVK()
//		.setPView(window);
//#endif

	//TODO: add subscription for it
	
	mWidth = win.getSize().x;
	mHeight = win.getSize().y;

	//mSurface = vk::raii::SurfaceKHR(mInstance, surfaceInfo);
	mCurrentWindowID = win.getId();
	VkSurfaceKHR surface = win.createVulkanSurface(*mInstance);

	SwapchainContextVk ctx;
	ctx.surface = vk::raii::SurfaceKHR(mInstance, surface);
	mSwapchains[mCurrentWindowID] = std::move(ctx);


	auto formats = mPhysicalDevice.getSurfaceFormatsKHR(*mSwapchains[mCurrentWindowID].surface);

	if ((formats.size() == 1) && (formats.at(0).format == vk::Format::eUndefined)) {
		mSurfaceFormat = {
			vk::Format::eB8G8R8A8Unorm,
			formats.at(0).colorSpace
		};
	} else {
		bool found = false;
		for (const auto& format : formats) {
			if (format.format == vk::Format::eB8G8R8A8Unorm) {
				mSurfaceFormat = format;
				found = true;
				break;
			}
		}
		if (!found) {
			mSurfaceFormat = formats.at(0);
		}
	}

	vk::CommandPoolCreateInfo commandPoolInfo{};
	commandPoolInfo.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
	commandPoolInfo.setQueueFamilyIndex(mQueueFamilyIndex);

	mCommandPool = mDevice.createCommandPool(commandPoolInfo);

	//mCurrentState.color_attachment_formats = {mSurfaceFormat.format};
	//mCurrentState.depth_stencil_format = ContextVK::DefaultDepthStencilFormat;

	createSwapchain(mCurrentWindowID, mWidth, mHeight);
	nextFrame();
	begin();
}

bool DriverVk::checkValidationLayerSupport(const std::vector<const char*>& validationLayers) {
	uint32_t nAvailableLayers;
	vkEnumerateInstanceLayerProperties(&nAvailableLayers, nullptr);

	std::vector<VkLayerProperties> availableLayers(nAvailableLayers);
	vkEnumerateInstanceLayerProperties(&nAvailableLayers, availableLayers.data());

	for (const auto& layerName : validationLayers) {
		bool layerFound = false;

		for (const auto& layerProperties : availableLayers) {
			if (strcmp(layerName, layerProperties.layerName) == 0) {
				layerFound = true;
				break;
			}
		}

		if (!layerFound) {
			return false;
		}
	}
	return true;
}

bool DriverVk::checkInstanceExtensionSupport(const std::vector<const char*>& extensionsToCheck) {
	uint32_t nAvailableExt = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &nAvailableExt, nullptr);

	std::vector<VkExtensionProperties> availableExt(nAvailableExt);
	vkEnumerateInstanceExtensionProperties(nullptr, &nAvailableExt, availableExt.data());

	for (const auto& proposedExt : extensionsToCheck) {
		bool hasExtension = false;
		for (const auto& extension : availableExt) {
			if (strcmp(proposedExt, extension.extensionName)) {
				hasExtension = true;
				break;
			}
		}

		if (!hasExtension)
			return false;
	}
	return true;
}


void DriverVk::begin() {
	working = true;

	setDirty(Dirty::VERTEX_BUFFER);
	setDirty(Dirty::INDEX_BUFFER);
	setDirty(Dirty::VIEWPORT);
	setDirty(Dirty::SCISSOR);
	setDirty(Dirty::RASTERIZATION_MODE);
	setDirty(Dirty::PRIMITIVE_MODE);
	setDirty(Dirty::DEPTH);
	setDirty(Dirty::STENCIL);
	setDirty(Dirty::BLENDING);
	setDirty(Dirty::CULLING);
	setDirty(Dirty::PIPELINE);
	setDirty(Dirty::TRIANGULATION_ORDER);

	//gContext->pipeline_state_dirty = true;
	//gContext->topology_dirty = true;
	//gContext->cull_mode_dirty = true;
	//gContext->front_face_dirty = true;
	//gContext->blend_mode_dirty = true;
	//gContext->depth_mode_dirty = true;
	//gContext->stencil_mode_dirty = true;

	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	std::cout << "vkBeginCommandBuffer\n";
	getCurrentFrame().mCommandBuffer.begin(beginInfo);

	auto tex = std::static_pointer_cast<TextureVk>(getCurrentFrame().mFrameBuffer->getTextures()[0]);
	//tex->setState(getCurrentFrame().mCommandBuffer, vk::ImageLayout::eColorAttachmentOptimal);
	//TODO:
	UtilityVk::SetImageMemoryBarrier(getCurrentFrame().mCommandBuffer, tex->mImagePtr, vk::ImageAspectFlagBits::eColor, tex->mCurrentState, vk::ImageLayout::eColorAttachmentOptimal);
	tex->mCurrentState = vk::ImageLayout::eColorAttachmentOptimal;

}

void DriverVk::end() {
	//gContext->working = false;

	deactivateRenderPass();

	auto tex = std::static_pointer_cast<TextureVk>(getCurrentFrame().mFrameBuffer->getTextures()[0]);
	tex->setState(getCurrentFrame().mCommandBuffer, vk::ImageLayout::ePresentSrcKHR);

	//UtilityVk::SetImageMemoryBarrier(getCurrentFrame().mCommandBuffer, tex->mImagePtr, vk::ImageAspectFlagBits::eColor, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR);

	std::cout << "vkEndCommandBuffer\n";
	getCurrentFrame().mCommandBuffer.end();

	const auto& frame = getCurrentFrame();

	mDevice.resetFences({*frame.fence});

	auto wait_dst_stage_mask = vk::PipelineStageFlags{
		vk::PipelineStageFlagBits::eAllCommands
	};

	// The acquire semaphore is chosen by mSemaphoreIndex in nextFrame(), NOT by the acquired image index (mFrameIndex).
	// Waiting on frames[mFrameIndex] would wait on a semaphore that was never signaled.
	const auto& image_acquired_semaphore = getCurrentSwapchainContext().frames.at(mSemaphoreIndex).mImageAcquiredSemaphore;

	auto submit_info = vk::SubmitInfo()
		.setWaitDstStageMask(wait_dst_stage_mask)
		.setWaitSemaphores(*image_acquired_semaphore)
		.setCommandBuffers(*frame.mCommandBuffer)
		.setSignalSemaphores(*frame.mRenderCompleteSemaphore);

	mQueue.submit(submit_info, *frame.fence);
}

void DriverVk::setPushConstant(ShaderType stage, uint32_t offset, uint32_t size, const void* data) {
	if (mCurrentState.mShader && *mCurrentState.mShader->mPipelineLayout) {
		auto vkStage = stage == ShaderType::VERTEX ? vk::ShaderStageFlagBits::eVertex : vk::ShaderStageFlagBits::eFragment;
		getCurrentFrame().mCommandBuffer.pushConstants(*mCurrentState.mShader->mPipelineLayout, vkStage, offset, size, data);
	}
}

vk::raii::Pipeline DriverVk::createState(const State& pipeline_state) {
	std::vector<vk::PipelineShaderStageCreateInfo> pipeline_shader_stage_create_info;
	if (*pipeline_state.mShader->mVertexShaderModule) {
		pipeline_shader_stage_create_info.push_back(vk::PipelineShaderStageCreateInfo()
			.setStage(vk::ShaderStageFlagBits::eVertex)
			.setModule(*pipeline_state.mShader->mVertexShaderModule)
			.setPName("main"));
	}
	if (*pipeline_state.mShader->mFragmentShaderModule) {
		pipeline_shader_stage_create_info.push_back(vk::PipelineShaderStageCreateInfo()
			.setStage(vk::ShaderStageFlagBits::eFragment)
			.setModule(*pipeline_state.mShader->mFragmentShaderModule)
			.setPName("main"));
	}
	if (*pipeline_state.mShader->mGeometryShaderModule) {
		pipeline_shader_stage_create_info.push_back(vk::PipelineShaderStageCreateInfo()
			.setStage(vk::ShaderStageFlagBits::eGeometry)
			.setModule(*pipeline_state.mShader->mGeometryShaderModule)
			.setPName("main"));
	}
	if (*pipeline_state.mShader->mTessellationControlShaderModule) {
		pipeline_shader_stage_create_info.push_back(vk::PipelineShaderStageCreateInfo()
			.setStage(vk::ShaderStageFlagBits::eTessellationControl)
			.setModule(*pipeline_state.mShader->mTessellationControlShaderModule)
			.setPName("main"));
	}
	if (*pipeline_state.mShader->mTessellationEvaluationShaderModule) {
		pipeline_shader_stage_create_info.push_back(vk::PipelineShaderStageCreateInfo()
			.setStage(vk::ShaderStageFlagBits::eTessellationEvaluation)
			.setModule(*pipeline_state.mShader->mTessellationEvaluationShaderModule)
			.setPName("main"));
	}

	auto topology = vk::PrimitiveTopology::eTriangleList;
	if (*pipeline_state.mShader->mTessellationControlShaderModule || *pipeline_state.mShader->mTessellationEvaluationShaderModule) {
		topology = vk::PrimitiveTopology::ePatchList;
	}

	auto pipeline_input_assembly_state_create_info = vk::PipelineInputAssemblyStateCreateInfo()
		.setTopology(topology);

	vk::PipelineTessellationStateCreateInfo pipeline_tessellation_state_create_info = vk::PipelineTessellationStateCreateInfo()
		.setPatchControlPoints(3); // Default to 3, but this can be dynamic via VK_EXT_extended_dynamic_state3 if needed

	auto pipeline_viewport_state_create_info = vk::PipelineViewportStateCreateInfo()
		.setViewportCount(1)
		.setScissorCount(1);

	auto pipeline_rasterization_state_create_info = vk::PipelineRasterizationStateCreateInfo()
		.setPolygonMode(vk::PolygonMode::eFill);

	auto pipeline_multisample_state_create_info = vk::PipelineMultisampleStateCreateInfo()
		.setRasterizationSamples(vk::SampleCountFlagBits::e1);

	auto pipeline_depth_stencil_state_create_info = vk::PipelineDepthStencilStateCreateInfo();

	auto fb = pipeline_state.mFrameBuffer ? pipeline_state.mFrameBuffer : getCurrentFrame().mFrameBuffer;

	auto pipeline_color_blend_state_create_info = vk::PipelineColorBlendStateCreateInfo()
		.setAttachmentCount((uint32_t)fb->getTextures().size());

	std::vector<vk::VertexInputBindingDescription> vertex_input_binding_descriptions;
	std::vector<vk::VertexInputAttributeDescription> vertex_input_attribute_descriptions;

	//for (size_t i = 0; i < pipeline_state.mShader->getReflection().size(); i++) {
		//const auto& input_layout = pipeline_state.input_layouts.at(i);

		//static const std::unordered_map<InputLayout::Rate, vk::VertexInputRate> InputRateMap = {
		//	{InputLayout::Rate::Vertex, vk::VertexInputRate::eVertex},
		//	{InputLayout::Rate::Instance, vk::VertexInputRate::eInstance},
		//};

		auto vertex_input_binding_description = vk::VertexInputBindingDescription()
			.setInputRate(vk::VertexInputRate::eVertex)
			.setBinding((uint32_t)0);

		vertex_input_binding_descriptions.push_back(vertex_input_binding_description);

		for (const auto& param : pipeline_state.mShader->getReflection().mInputParams) {
			auto vertex_input_attribute_description = vk::VertexInputAttributeDescription()
				.setBinding((uint32_t)0)
				.setLocation(param.mLocation)
				.setFormat(FormatMap.at(param.mFormat))
				.setOffset((uint32_t)param.mOffset);

			vertex_input_attribute_descriptions.push_back(vertex_input_attribute_description);
		}
	//}

	// TODO: use dynamic state for InputLayout
	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_EXT_vertex_input_dynamic_state.html

	auto pipeline_vertex_input_state_create_info = vk::PipelineVertexInputStateCreateInfo()
		.setVertexBindingDescriptions(vertex_input_binding_descriptions)
		.setVertexAttributeDescriptions(vertex_input_attribute_descriptions);

	auto dynamic_states = {
		vk::DynamicState::eViewport,
		vk::DynamicState::eScissor,
		vk::DynamicState::ePrimitiveTopology,
		vk::DynamicState::eLineWidth,
		vk::DynamicState::eCullMode,
		vk::DynamicState::eFrontFace,
		vk::DynamicState::eVertexInputBindingStride,
		vk::DynamicState::eDepthTestEnable,
		vk::DynamicState::eDepthCompareOp,
		vk::DynamicState::eDepthWriteEnable,
		vk::DynamicState::eColorWriteMaskEXT,
		vk::DynamicState::eColorBlendEquationEXT,
		vk::DynamicState::eColorBlendEnableEXT,
		vk::DynamicState::eStencilTestEnable
	};

	auto pipeline_dynamic_state_create_info = vk::PipelineDynamicStateCreateInfo()
		.setDynamicStates(dynamic_states);

	std::vector<vk::Format> color_attachment_formats;
	for (auto& e : fb->getTextures()) {
		color_attachment_formats.push_back(FormatMap.at(e->getFormat()));
	}
	auto depth_stencil_format = FormatMap.at(fb->getDepth()->getFormat());
	auto pipeline_rendering_create_info = vk::PipelineRenderingCreateInfo()
		.setColorAttachmentFormats(color_attachment_formats)
		.setDepthAttachmentFormat(depth_stencil_format)
		.setStencilAttachmentFormat(depth_stencil_format);

	auto graphics_pipeline_create_info = vk::GraphicsPipelineCreateInfo()
		.setLayout(*pipeline_state.mShader->mPipelineLayout)
		.setFlags(vk::PipelineCreateFlagBits())
		.setStages(pipeline_shader_stage_create_info)
		.setPVertexInputState(&pipeline_vertex_input_state_create_info)
		.setPInputAssemblyState(&pipeline_input_assembly_state_create_info)
		.setPViewportState(&pipeline_viewport_state_create_info)
		.setPRasterizationState(&pipeline_rasterization_state_create_info)
		.setPMultisampleState(&pipeline_multisample_state_create_info)
		.setPDepthStencilState(&pipeline_depth_stencil_state_create_info)
		.setPColorBlendState(&pipeline_color_blend_state_create_info) // TODO: this can be nullptr https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkGraphicsPipelineCreateInfo.html
		.setPDynamicState(&pipeline_dynamic_state_create_info)
		.setRenderPass(nullptr)
		.setPNext(&pipeline_rendering_create_info);

	if (topology == vk::PrimitiveTopology::ePatchList) {
		graphics_pipeline_create_info.setPTessellationState(&pipeline_tessellation_state_create_info);
	}

	return mDevice.createGraphicsPipeline(nullptr, graphics_pipeline_create_info);
}


static vk::IndexType GetIndexTypeFromStride(size_t stride) {
	return stride == 2 ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
}

void DriverVk::EnsureVertexBuffers(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::VERTEX_BUFFER))
		return;
	clearDirty(Dirty::VERTEX_BUFFER);

	if (!mVertexBuffer || !*mVertexBuffer->getBuffer()) {
		LOG_ERROR << "DriverVk: vertex buffer is not set or empty";
		return;
	}

	std::vector<vk::Buffer> buffers;
	std::vector<vk::DeviceSize> offsets;
	std::vector<vk::DeviceSize> strides;

	//for (auto vertex_buffer : gContext->vertex_buffers) {
		buffers.push_back(*mVertexBuffer->getBuffer());
		offsets.push_back(0);
		strides.push_back(mVertexBuffer->getStride());
	//}

	cmdlist.bindVertexBuffers2(0, buffers, offsets, nullptr, strides);
}

void DriverVk::EnsureIndexBuffer(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::INDEX_BUFFER))
		return;
	clearDirty(Dirty::INDEX_BUFFER);

	if (!mIndexBuffer || !*mIndexBuffer->getBuffer()) {
		LOG_ERROR << "DriverVk: index buffer is not set or empty";
		return;
	}

	auto index_type = GetIndexTypeFromStride(mIndexBuffer->getStride());
	cmdlist.bindIndexBuffer(*mIndexBuffer->getBuffer(), 0, index_type);
}

void DriverVk::EnsureTopology(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::PRIMITIVE_MODE))
		return;
	clearDirty(Dirty::PRIMITIVE_MODE);

	static const std::unordered_map<PrimitiveMode, vk::PrimitiveTopology> TopologyMap = {
		{PrimitiveMode::POINTS, vk::PrimitiveTopology::ePointList},
		{PrimitiveMode::LINES, vk::PrimitiveTopology::eLineList},
		{PrimitiveMode::LINE_STRIP, vk::PrimitiveTopology::eLineStrip},
		{PrimitiveMode::TRIANGLES, vk::PrimitiveTopology::eTriangleList},
		{PrimitiveMode::TRIANGLE_STRIP, vk::PrimitiveTopology::eTriangleStrip},
		{PrimitiveMode::LINES_ADJACENCY, vk::PrimitiveTopology::eLineListWithAdjacency},
		{PrimitiveMode::LINE_STRIP_ADJACENCY, vk::PrimitiveTopology::eLineStripWithAdjacency},
		{PrimitiveMode::TRIANGLES_ADJACENCY, vk::PrimitiveTopology::eTriangleListWithAdjacency},
		{PrimitiveMode::TRIANGLE_STRIP_ADJACENCY, vk::PrimitiveTopology::eTriangleStripWithAdjacency},
		{PrimitiveMode::PATCHES, vk::PrimitiveTopology::ePatchList},
	};

	auto topology = TopologyMap.at(mPrimitiveMode);

	// Pipeline with tessellation is always created with ePatchList (see createState),
	// dynamic topology must be the same class, otherwise Vulkan validation fails.
	if (mCurrentState.mShader &&
		(*mCurrentState.mShader->mTessellationControlShaderModule || *mCurrentState.mShader->mTessellationEvaluationShaderModule)) {
		topology = vk::PrimitiveTopology::ePatchList;
	}

	cmdlist.setPrimitiveTopology(topology);
}

void DriverVk::EnsureViewport(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::VIEWPORT))
		return;
	clearDirty(Dirty::VIEWPORT);

	auto width = static_cast<float>(mWidth);
	auto height = static_cast<float>(mHeight);

	auto value = mViewport.value_or(Viewport{{0.0f, 0.0f}, {width, height}});

	auto viewport = vk::Viewport()
		.setX(value.mPosition.x)
		.setY(value.mSize.y - value.mPosition.y)
		.setWidth(value.mSize.x)
		.setHeight(-value.mSize.y)
		.setMinDepth(value.mMinDepth)
		.setMaxDepth(value.mMaxDepth);

	cmdlist.setViewport(0, {viewport});
}

void DriverVk::EnsureScissor(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::SCISSOR))
		return;
	clearDirty(Dirty::SCISSOR);

	auto width = static_cast<float>(mWidth);
	auto height = static_cast<float>(mHeight);

	auto value = mScissor.value_or(Scissor{{0.0f, 0.0f}, {width, height}});

	auto rect = vk::Rect2D()
		.setOffset({static_cast<int32_t>(value.mPosition.x), static_cast<int32_t>(value.mPosition.y)})
		.setExtent({static_cast<uint32_t>(value.mSize.x), static_cast<uint32_t>(value.mSize.y)});

	if (rect.offset.x < 0) {
		rect.extent.width -= rect.offset.x;
		rect.offset.x = 0;
	}

	if (rect.offset.y < 0) {
		rect.extent.height -= rect.offset.y;
		rect.offset.y = 0;
	}

	if (rect.extent.width < 0)
		rect.extent.width = 0;

	if (rect.extent.height < 0)
		rect.extent.height = 0;

	cmdlist.setScissor(0, {rect});
}

void DriverVk::EnsureCullMode(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::CULLING))
		return;
	clearDirty(Dirty::CULLING);

	const static std::unordered_map<CullFace, vk::CullModeFlags> CullModeMap = {
		{CullFace::NONE, vk::CullModeFlagBits::eNone},
		{CullFace::FRONT, vk::CullModeFlagBits::eFront},
		{CullFace::BACK, vk::CullModeFlagBits::eBack},
	};

	cmdlist.setCullMode(CullModeMap.at(mCullFace));
}

void DriverVk::EnsureFrontFace(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::TRIANGULATION_ORDER))
		return;
	clearDirty(Dirty::TRIANGULATION_ORDER);

	const static std::unordered_map<TriangleOrientation, vk::FrontFace> FrontFaceMap = {
		{TriangleOrientation::CW, vk::FrontFace::eClockwise},
		{TriangleOrientation::CCW, vk::FrontFace::eCounterClockwise},
	};

	cmdlist.setFrontFace(FrontFaceMap.at(mTriangleOrientation));
}

void DriverVk::EnsureBlendMode(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::BLENDING))
		return;
	clearDirty(Dirty::BLENDING);

	static const std::unordered_map<BlendMode, vk::BlendFactor> BlendFactorMap = {
		{BlendMode::ONE, vk::BlendFactor::eOne},
		{BlendMode::ZERO, vk::BlendFactor::eZero},
		{BlendMode::SRC_COLOR, vk::BlendFactor::eSrcColor},
		{BlendMode::ONE_MINUS_SRC_COLOR, vk::BlendFactor::eOneMinusSrcColor},
		{BlendMode::SRC_ALPHA, vk::BlendFactor::eSrcAlpha},
		{BlendMode::ONE_MINUS_SRC_ALPHA, vk::BlendFactor::eOneMinusSrcAlpha},
		{BlendMode::DST_COLOR, vk::BlendFactor::eDstColor},
		{BlendMode::ONE_MINUS_DST_COLOR, vk::BlendFactor::eOneMinusDstColor},
		{BlendMode::DST_ALPHA, vk::BlendFactor::eDstAlpha},
		{BlendMode::ONE_MINUS_DST_ALPHA, vk::BlendFactor::eOneMinusDstAlpha}
	};

	static const std::unordered_map<BlendFunction, vk::BlendOp> BlendFuncMap = {
		{BlendFunction::ADD, vk::BlendOp::eAdd},
		{BlendFunction::SUB, vk::BlendOp::eSubtract},
		{BlendFunction::REVERT_SUB, vk::BlendOp::eReverseSubtract},
		{BlendFunction::MIN, vk::BlendOp::eMin},
		{BlendFunction::MAX, vk::BlendOp::eMax},
	};

	const auto blend_mode = mBlendMode.value_or(Blending(BlendMode::ONE, BlendMode::ZERO));

	auto color_mask = vk::ColorComponentFlags();

	if ((blend_mode.mColorMask & Color::R) != Color::NONE)
		color_mask |= vk::ColorComponentFlagBits::eR;

	if ((blend_mode.mColorMask & Color::G) != Color::NONE)
		color_mask |= vk::ColorComponentFlagBits::eG;

	if ((blend_mode.mColorMask & Color::B) != Color::NONE)
		color_mask |= vk::ColorComponentFlagBits::eB;

	if ((blend_mode.mColorMask & Color::A) != Color::NONE)
		color_mask |= vk::ColorComponentFlagBits::eA;

	auto color_blend_equation = vk::ColorBlendEquationEXT()
		.setSrcColorBlendFactor(BlendFactorMap.at(blend_mode.mColorSrc))
		.setDstColorBlendFactor(BlendFactorMap.at(blend_mode.mColorDst))
		.setColorBlendOp(BlendFuncMap.at(blend_mode.mColorFunc))
		.setSrcAlphaBlendFactor(BlendFactorMap.at(blend_mode.mAlphaSrc))
		.setDstAlphaBlendFactor(BlendFactorMap.at(blend_mode.mAlphaDst))
		.setAlphaBlendOp(BlendFuncMap.at(blend_mode.mAlphaFunc));

	std::vector<uint32_t> blend_enable_array;
	std::vector<vk::ColorComponentFlags> color_mask_array;
	std::vector<vk::ColorBlendEquationEXT> color_blend_equation_array;

	if (!mCurrentState.mFrameBuffer) {
		blend_enable_array = {mBlendMode.has_value()};
		color_mask_array = {color_mask};
		color_blend_equation_array = {color_blend_equation};
	} else {
		for (size_t i = 0; i < mCurrentState.mFrameBuffer->getTextures().size(); i++) {
			blend_enable_array.push_back(mBlendMode.has_value());
			color_mask_array.push_back(color_mask);
			color_blend_equation_array.push_back(color_blend_equation);
		}
	}

	cmdlist.setColorBlendEnableEXT(0, blend_enable_array);
	cmdlist.setColorWriteMaskEXT(0, color_mask_array);
	cmdlist.setColorBlendEquationEXT(0, color_blend_equation_array);
}

const static std::unordered_map<DepthFunction, vk::CompareOp> CompareOpMap = {
	{DepthFunction::ALWAYS, vk::CompareOp::eAlways},
	{DepthFunction::EQUAL, vk::CompareOp::eNever},
	{DepthFunction::LESS, vk::CompareOp::eLess},
	{DepthFunction::EQUAL, vk::CompareOp::eEqual},
	{DepthFunction::NOT_EQUAL, vk::CompareOp::eNotEqual},
	{DepthFunction::LESS, vk::CompareOp::eLessOrEqual},
	{DepthFunction::GREATER, vk::CompareOp::eGreater},
	{DepthFunction::GREATER_EQUAL, vk::CompareOp::eGreaterOrEqual}
};

void DriverVk::EnsureDepthMode(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::DEPTH))
		return;
	clearDirty(Dirty::DEPTH);

	if (mDepthMode) {
		cmdlist.setDepthTestEnable(true);
		cmdlist.setDepthWriteEnable(mDepthMode.value().mWriteMask);
		cmdlist.setDepthCompareOp(CompareOpMap.at(mDepthMode.value().mFunc));
	} else {
		cmdlist.setDepthTestEnable(false);
		cmdlist.setDepthWriteEnable(false);
		cmdlist.setDepthCompareOp(vk::CompareOp::eAlways);
	}
}

void DriverVk::EnsureStencilMode(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::STENCIL))
		return;
	clearDirty(Dirty::STENCIL);

	cmdlist.setStencilTestEnable(mStencilMode.has_value());
}

void DriverVk::EnsureGraphicsPipelineState(vk::raii::CommandBuffer& cmdlist) {
	// The pipeline depends on the current shader and framebuffer, which can change between draws
	// (setShader/setFrameBuffer do not raise the dirty flag), so compare by name as well.
	const auto name = mCurrentState.getName();
	if (!isDirty(Dirty::PIPELINE) && name == mBoundPipelineName)
		return;
	clearDirty(Dirty::PIPELINE);

	if (!mStates.contains(name)) {
		auto pipeline = createState(mCurrentState);
		mStates.insert({name, std::move(pipeline)});
	}

	const auto& pipeline = mStates.at(name);
	cmdlist.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
	mBoundPipelineName = name;

	graphics_pipeline_ignore_bindings.clear();
}

void DriverVk::EnsureGraphicsDescriptors(vk::raii::CommandBuffer& cmdlist) {
	const auto& pipeline_layout = mCurrentState.mShader->mPipelineLayout;

	// Descriptor bindings are built once by ShaderVk::createPipelineLayout():
	// push constants are excluded (they are not descriptors), all stages and sampler types are covered.
	const auto& required_descriptor_bindings = mCurrentState.mShader->mRequiredDescriptorBindings;

	//auto descriptor_set_layout_create_info = vk::DescriptorSetLayoutCreateInfo()
	//	.setFlags(vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR)
	//	.setBindings(required_descriptor_bindings);
	//
	//auto descriptor_set_layout = gContext->device.createDescriptorSetLayout(descriptor_set_layout_create_info);
	//
	//auto pipeline_layout_create_info = vk::PipelineLayoutCreateInfo()
	//	.setSetLayouts(*descriptor_set_layout);
	//
	//auto pipeline_layout = gContext->device.createPipelineLayout(pipeline_layout_create_info);

	auto& ignore_bindings = graphics_pipeline_ignore_bindings;
	

	PushDescriptors(cmdlist, vk::PipelineBindPoint::eGraphics, pipeline_layout, required_descriptor_bindings, ignore_bindings);

	for (const auto& descriptor_binding : required_descriptor_bindings) {
		ignore_bindings.insert(descriptor_binding.binding);
	}
}

void DriverVk::EnsureGraphicsState(bool draw_indexed) {
	auto& cmdlist = getCurrentFrame().mCommandBuffer;

	EnsureMemoryState(cmdlist, vk::PipelineStageFlagBits2::eAllGraphics);
	EnsureGraphicsPipelineState(cmdlist);
	EnsureGraphicsDescriptors(cmdlist);
	EnsureVertexBuffers(cmdlist);

	if (draw_indexed)
		EnsureIndexBuffer(cmdlist);

	EnsureTopology(cmdlist);
	EnsureViewport(cmdlist);
	EnsureScissor(cmdlist);
	EnsureCullMode(cmdlist);
	EnsureFrontFace(cmdlist);
	EnsureBlendMode(cmdlist);
	EnsureDepthMode(cmdlist);
	EnsureStencilMode(cmdlist);
	activateRenderPass();
}

void DriverVk::wait() {
	auto it = mSwapchains.find(mCurrentWindowID);
	if (it == mSwapchains.end() || it->second.frames.empty() || mFrameIndex >= it->second.frames.size()) {
		return;
	}
	auto& frame = it->second.frames.at(mFrameIndex);
	auto wait_result = mDevice.waitForFences({*frame.fence}, true, UINT64_MAX);
	(void)wait_result;
	frame.destroyDeferred.clear();
}

void DriverVk::createSwapchain(unsigned int windowID, uint32_t width, uint32_t height) {
	auto& ctx = mSwapchains[windowID];
	auto surface_capabilities = mPhysicalDevice.getSurfaceCapabilitiesKHR(*ctx.surface);

	// https://github.com/nvpro-samples/nvpro_core/blob/f2c05e161bba9ab9a8c96c0173bf0edf7c168dfa/nvvk/swapchain_vk.cpp#L143
	// Determine the number of VkImage's to use in the swap chain (we desire to
	// own only 1 image at a time, besides the images being displayed and
	// queued for display):

	uint32_t desired_number_of_swapchain_images = surface_capabilities.minImageCount + 1;

	if ((surface_capabilities.maxImageCount > 0) && (desired_number_of_swapchain_images > surface_capabilities.maxImageCount)) {
		// Application must settle for fewer images than desired:
		desired_number_of_swapchain_images = surface_capabilities.maxImageCount;
	}

	auto max_width = surface_capabilities.maxImageExtent.width;
	auto max_height = surface_capabilities.maxImageExtent.height;

	ctx.width = glm::min(width, max_width);
	ctx.height = glm::min(height, max_height);

	auto image_extent = vk::Extent2D()
		.setWidth(ctx.width)
		.setHeight(ctx.height);

	auto format = mSurfaceFormat.format;

	auto swapchain_info = vk::SwapchainCreateInfoKHR()
		.setSurface(*ctx.surface)
		.setMinImageCount(desired_number_of_swapchain_images)
		.setImageFormat(format)
		.setImageColorSpace(mSurfaceFormat.colorSpace)
		.setImageExtent(image_extent)
		.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc)
		.setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity)
		.setImageArrayLayers(1)
		.setImageSharingMode(vk::SharingMode::eExclusive)
		.setQueueFamilyIndices(mQueueFamilyIndex)
		.setPresentMode(vk::PresentModeKHR::eFifo)
		.setClipped(true)
		.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
		.setOldSwapchain(*ctx.swapchain);

	ctx.swapchain = mDevice.createSwapchainKHR(swapchain_info);

	auto backbuffers = ctx.swapchain.getImages();

	ctx.frames.clear();

	for (auto& backbuffer : backbuffers) {
		Frame frame;

		auto fence_info = vk::FenceCreateInfo()
			.setFlags(vk::FenceCreateFlagBits::eSignaled);

		frame.fence = mDevice.createFence(fence_info);

		frame.mImageAcquiredSemaphore = mDevice.createSemaphore({});
		frame.mRenderCompleteSemaphore = mDevice.createSemaphore({});

		auto command_buffer_allocate_info = vk::CommandBufferAllocateInfo()
			.setCommandBufferCount(1)
			.setLevel(vk::CommandBufferLevel::ePrimary)
			.setCommandPool(*mCommandPool);

		auto command_buffers = mDevice.allocateCommandBuffers(command_buffer_allocate_info);

		frame.mCommandBuffer = std::move(command_buffers.at(0));

		
		auto frameTexture = std::make_shared<TextureVk>(ctx.width, ctx.height, format, backbuffer);
		TextureResource depthRes;
		depthRes.width = ctx.width;
		depthRes.height = ctx.height;
		depthRes.depth = 1;
		depthRes.mipMapCount = 1;
		depthRes.useMipmap = false;
		depthRes.texType = TextureType::DEPTH;
		depthRes.pixelType = PixelFormat::DEPTH_32_FLOAT_STENCIL_8_UINT;
		auto frameDepth = std::make_shared<TextureVk>(depthRes, std::vector<void*>{});
		frame.mFrameBuffer = std::make_shared<FrameBufferVk>(std::vector<std::shared_ptr<TextureInterface>>{frameTexture}, frameDepth);

		//frame.swapchain_texture = std::make_shared<TextureVk>(ctx.width, ctx.height, format, backbuffer);
		//frame.swapchain_target = std::make_shared<RenderTargetVk>(frame.swapchain_texture);

		ctx.frames.push_back(std::move(frame));
	}

	mFrameIndex = 0;
	mSemaphoreIndex = 0;
}

void DriverVk::nextFrame() {
	const auto& image_acquired_semaphore = getCurrentSwapchainContext().frames.at(mSemaphoreIndex).mImageAcquiredSemaphore;

	auto res = getCurrentSwapchainContext().swapchain.acquireNextImage(UINT64_MAX, *image_acquired_semaphore);

	mFrameIndex = res.value;
}


uint32_t DriverVk::getBackbufferWidth() {
	return mCurrentState.mFrameBuffer ? mCurrentState.mFrameBuffer->getWidth() : getCurrentSwapchainContext().width;
}

uint32_t DriverVk::getBackbufferHeight() {
	return mCurrentState.mFrameBuffer ? mCurrentState.mFrameBuffer->getHeight() : getCurrentSwapchainContext().height;
}

vk::Format DriverVk::getBackbufferFormat() {
	// TODO: wtf when mrt
	return mCurrentState.mFrameBuffer ? 
		FormatMap.at(std::static_pointer_cast<TextureVk>(mCurrentState.mFrameBuffer->getTextures().at(0))->getFormat()) :
	FormatMap.at(PixelFormat::RGBA_INT); //gContext->surface_format.format;
}

void DriverVk::beginRenderPass() {
	//assert(!gContext->render_pass_active);
	render_pass_active = true;

	auto targets = mCurrentState.mFrameBuffer;

	if (!targets)
		targets = getCurrentFrame().mFrameBuffer;

	std::vector<vk::RenderingAttachmentInfo> color_attachments;
	std::optional<vk::RenderingAttachmentInfo> depth_stencil_attachment;

	for (auto target : targets->getTextures()) {
		auto color_attachment = vk::RenderingAttachmentInfo()
			.setImageView(*static_cast<TextureVk*>(target.get())->mImageView)
			.setImageLayout(vk::ImageLayout::eGeneral)
			.setLoadOp(vk::AttachmentLoadOp::eLoad)
			.setStoreOp(vk::AttachmentStoreOp::eStore);

		color_attachments.push_back(color_attachment);

		if (!depth_stencil_attachment.has_value()) {
			depth_stencil_attachment = vk::RenderingAttachmentInfo()
				.setImageView(*static_cast<TextureVk*>(targets->getDepth().get())->mImageView)
				.setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
				.setLoadOp(vk::AttachmentLoadOp::eLoad)
				.setStoreOp(vk::AttachmentStoreOp::eStore);
		}
	}

	auto width = getBackbufferWidth();
	auto height = getBackbufferHeight();

	auto rendering_info = vk::RenderingInfo()
		.setRenderArea({{0, 0}, {width, height}})
		.setLayerCount(1)
		.setColorAttachments(color_attachments);

	if (depth_stencil_attachment.has_value()) {
		rendering_info.setPDepthAttachment(&depth_stencil_attachment.value());
		rendering_info.setPStencilAttachment(&depth_stencil_attachment.value());
	}
	std::cout << "beginRendering\n";
	getCurrentFrame().mCommandBuffer.beginRendering(rendering_info);
}

void DriverVk::endRenderPass() {
	//assert(gContext->render_pass_active);
	render_pass_active = false;

	std::cout << "endRendering\n";
	getCurrentFrame().mCommandBuffer.endRendering();
}

void DriverVk::activateRenderPass() {
	if (render_pass_active)
		return;

	beginRenderPass();
}

void DriverVk::deactivateRenderPass() {
	if (!render_pass_active)
		return;

	endRenderPass();
}

void DriverVk::destroyDeferred(VulkanObject&& object) {
	if (mShuttingDown) {
		return;
	}
	auto it = mSwapchains.find(mCurrentWindowID);
	if (it == mSwapchains.end() || it->second.frames.empty() || mFrameIndex >= it->second.frames.size()) {
		return;
	}
	it->second.frames[mFrameIndex].destroyDeferred.push_back(std::move(object));
}

std::vector<DriverVk::VulkanObject>& DriverVk::getDestroyDeferredObjects() {
	return getCurrentFrame().destroyDeferred;
}

void DriverVk::EnsureMemoryState(const vk::raii::CommandBuffer& cmdbuf, vk::PipelineStageFlags2 stage) {
	if (mCurrentMemoryStage == stage) return;

	deactivateRenderPass();
	UtilityVk::SetMemoryBarrier(cmdbuf, mCurrentMemoryStage, stage);

	mCurrentMemoryStage = stage;
}

void DriverVk::PushDescriptorBuffer(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
	const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding, vk::DescriptorType type,
	vk::Buffer buffer) {
	auto descriptor_buffer_info = vk::DescriptorBufferInfo()
		.setBuffer(buffer)
		.setRange(VK_WHOLE_SIZE);

	auto write_descriptor_set = vk::WriteDescriptorSet()
		.setDstBinding(binding)
		.setDescriptorCount(1)
		.setDescriptorType(type)
		.setBufferInfo(descriptor_buffer_info);

	cmdlist.pushDescriptorSetKHR(pipeline_bind_point,
		*pipeline_layout, 0, write_descriptor_set);
}

void DriverVk::PushDescriptorTexture(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
	const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding) {
	//if (!gContext->sampler_states.contains(gContext->sampler_state)) {
	//	auto sampler = CreateSamplerState(gContext->sampler_state);
	//	gContext->sampler_states.insert({gContext->sampler_state, std::move(sampler)});
	//}

	auto texture = mTextures.at(binding);
	if (texture->mCurrentState != vk::ImageLayout::eGeneral) {
		// Image layout transitions (pipeline barriers) are not allowed inside a dynamic rendering scope.
		// The render pass is reactivated at the end of EnsureGraphicsState().
		deactivateRenderPass();
	}
	texture->setState(cmdlist, vk::ImageLayout::eGeneral);

	const auto& sampler = texture->mSampler;

	auto descriptor_image_info = vk::DescriptorImageInfo()
		.setSampler(*sampler)
		.setImageView(*(texture->mImageView))
		.setImageLayout(vk::ImageLayout::eGeneral);

	auto write_descriptor_set = vk::WriteDescriptorSet()
		.setDstBinding(binding)
		.setDescriptorCount(1)
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
		.setImageInfo(descriptor_image_info);

	cmdlist.pushDescriptorSetKHR(pipeline_bind_point,
		*pipeline_layout, 0, write_descriptor_set);
}

void DriverVk::PushDescriptorUniformBuffer(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
	const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding) {
	auto buffer = mUniformBuffers.at(binding);

	PushDescriptorBuffer(cmdlist, pipeline_bind_point, pipeline_layout, binding,
		vk::DescriptorType::eUniformBuffer, *buffer->getBuffer());
}

void DriverVk::PushDescriptorStorageImage(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
	const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding) {
	//auto texture = gContext->render_targets.at(0)->getTexture();
	//texture->ensureState(cmdlist, vk::ImageLayout::eGeneral);
	//
	//auto descriptor_image_info = vk::DescriptorImageInfo()
	//	.setImageLayout(vk::ImageLayout::eGeneral)
	//	.setImageView(*texture->getImageView());
	//
	//auto write_descriptor_set = vk::WriteDescriptorSet()
	//	.setDstBinding(binding)
	//	.setDescriptorCount(1)
	//	.setDescriptorType(vk::DescriptorType::eStorageImage)
	//	.setImageInfo(descriptor_image_info);
	//
	//cmdlist.pushDescriptorSetKHR(pipeline_bind_point,
	//	*pipeline_layout, 0, write_descriptor_set);
}

void DriverVk::PushDescriptorStorageBuffer(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
	const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding) {
	auto buffer = mStorageBuffers.at(binding);

	PushDescriptorBuffer(cmdlist, pipeline_bind_point, pipeline_layout, binding,
		vk::DescriptorType::eStorageBuffer, *buffer->getBuffer());
}

void DriverVk::PushDescriptors(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
	const vk::raii::PipelineLayout& pipeline_layout, const std::vector<vk::DescriptorSetLayoutBinding>& required_descriptor_bindings,
	const std::unordered_set<uint32_t>& ignore_bindings = {}) {


	for (const auto& required_descriptor_binding : required_descriptor_bindings) {
		auto binding = required_descriptor_binding.binding;

		if (ignore_bindings.contains(binding))
			continue;

		auto type = required_descriptor_binding.descriptorType;

		if (type == vk::DescriptorType::eCombinedImageSampler) {
			PushDescriptorTexture(cmdlist, pipeline_bind_point, pipeline_layout, binding);
		}
		if (type == vk::DescriptorType::eUniformBuffer) {
			PushDescriptorUniformBuffer(cmdlist, pipeline_bind_point, pipeline_layout, binding);

		}
		if (type == vk::DescriptorType::eStorageImage) {
			PushDescriptorStorageImage(cmdlist, pipeline_bind_point, pipeline_layout, binding);

		}
		if (type == vk::DescriptorType::eStorageBuffer) {
			PushDescriptorStorageBuffer(cmdlist, pipeline_bind_point, pipeline_layout, binding);

		}
	}
}

void DriverVk::resize(size_t width, size_t height) {
	end();
	wait();
	mWidth = static_cast<uint32_t>(width);
	mHeight = static_cast<uint32_t>(height);
	createSwapchain(mCurrentWindowID, mWidth, mHeight);
	nextFrame();
	begin();
}

//void DriverVk::setVsync(bool value) {
//	// TODO: implement
//}

void DriverVk::setPrimitiveMode(PrimitiveMode topology) {
	setDirty(Dirty::PRIMITIVE_MODE);
	mPrimitiveMode = topology;
}

void DriverVk::setRasterization(RasterizationMode mode)
{
	setDirty(Dirty::PRIMITIVE_MODE);
	mRasterizationMode = mode;
}

void DriverVk::setViewport(const Viewport& viewport) {
	setDirty(Dirty::VIEWPORT);
	mViewport = viewport;
}

void DriverVk::setScissor(const Scissor& scissor) {
	setDirty(Dirty::SCISSOR);
	mScissor = scissor;
}

void DriverVk::setTexture(size_t binding, std::shared_ptr<TextureInterface> handle) {
	mTextures[binding] = std::static_pointer_cast<TextureVk>(handle);
	graphics_pipeline_ignore_bindings.erase(binding);
}

void DriverVk::setShader(std::shared_ptr<ShaderInterface> shader) {
	mCurrentState.mShader = std::static_pointer_cast<ShaderVk>(shader);
	// Topology depends on the shader (tessellation -> patch list)
	setDirty(Dirty::PRIMITIVE_MODE);
}

void DriverVk::setVertexBuffer(const std::shared_ptr<VertexBufferInterface> buffer) {
	setDirty(Dirty::VERTEX_BUFFER);
	mVertexBuffer = std::static_pointer_cast<VertexBufferVk>(buffer);
}

void DriverVk::setIndexBuffer(const std::shared_ptr<IndexBufferInterface> buffer) {
	setDirty(Dirty::INDEX_BUFFER);
	mIndexBuffer = std::static_pointer_cast<IndexBufferVk>(buffer);;
}

void DriverVk::setUniformBuffer(size_t binding, std::shared_ptr<UniformBufferInterface> handle) {
	mUniformBuffers[binding] = std::static_pointer_cast<UniformBufferVk>(handle);
	graphics_pipeline_ignore_bindings.erase(binding);
}

void DriverVk::setStorageBuffer(size_t binding, std::shared_ptr<StorageBufferInterface> handle) {
	mStorageBuffers[binding] = std::static_pointer_cast<StorageBufferVk>(handle);
	//graphics_pipeline_ignore_bindings.erase(binding);
}

void DriverVk::setTexture(const std::string& name, std::shared_ptr<TextureInterface> data) {
	if (!mCurrentState.mShader) return;
	const auto& ref = mCurrentState.mShader->getReflection();
	if (ref.mNameToUniforms.contains(name)) {
		size_t idx = ref.mNameToUniforms.at(name);
		setTexture(ref.mUniforms[idx].mBind, data);
	}
}

void DriverVk::setUniformBuffer(const std::string& name, std::shared_ptr<UniformBufferInterface> data) {
	if (!mCurrentState.mShader) return;
	const auto& ref = mCurrentState.mShader->getReflection();
	if (ref.mNameToUniforms.contains(name)) {
		size_t idx = ref.mNameToUniforms.at(name);
		setUniformBuffer(ref.mUniforms[idx].mBind, data);
	}
}

void DriverVk::setStorageBuffer(const std::string& name, std::shared_ptr<StorageBufferInterface> data) {
	if (!mCurrentState.mShader) return;
	const auto& ref = mCurrentState.mShader->getReflection();
	if (ref.mNameToUniforms.contains(name)) {
		size_t idx = ref.mNameToUniforms.at(name);
		setStorageBuffer(ref.mUniforms[idx].mBind, data);
	}
}

void DriverVk::setMSAA(bool value) {
	mMSAA = value;
}

void DriverVk::setBlending(const Blending& value) {
	setDirty(Dirty::BLENDING);
	mBlendMode = value;
}

void DriverVk::setDepth(const Depth& depth_mode) {
	setDirty(Dirty::DEPTH);
	mDepthMode = depth_mode;
}

void DriverVk::setStencil(const Stencil& stencil_mode) {
	setDirty(Dirty::STENCIL);
	mStencilMode = stencil_mode;
}

void DriverVk::setCull(CullFace cull_mode) {
	setDirty(Dirty::CULLING);
	mCullFace = cull_mode;
}

//void DriverVk::setTextureAddress(TextureAddress value) {
//	gContext->sampler_state.texture_address = value;
//}

void DriverVk::setTriangleOrientation(TriangleOrientation value) {
	setDirty(Dirty::TRIANGULATION_ORDER);
	mTriangleOrientation = value;
}

void DriverVk::clear(bool clearColor, bool clearDepth, bool clearStencil) {
	activateRenderPass();

	auto width = getBackbufferWidth();
	auto height = getBackbufferHeight();

	auto clear_rect = vk::ClearRect()
		.setBaseArrayLayer(0)
		.setLayerCount(1)
		.setRect({{0, 0}, {width, height}});

	if (clearColor) {

		auto clear_color_value = vk::ClearColorValue()
			.setFloat32({mClearColor.r, mClearColor.g, mClearColor.b, mClearColor.a});

		auto clear_value = vk::ClearValue()
			.setColor(clear_color_value);

		auto attachment = vk::ClearAttachment()
			.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setColorAttachment(0) // TODO: clear all attachments
			.setClearValue(clear_value);

		getCurrentFrame().mCommandBuffer.clearAttachments({attachment}, {clear_rect});
	}

	if (clearDepth || clearStencil) {
		auto clear_depth_stencil_value = vk::ClearDepthStencilValue()
			.setDepth(mDepthValue)
			.setStencil((uint32_t)mStencilValue); // TODO: maybe we should change argument uint8_t -> uint32_t

		auto clear_value = vk::ClearValue()
			.setDepthStencil(clear_depth_stencil_value);

		auto aspect_mask = vk::ImageAspectFlags();

		if (clearDepth)
			aspect_mask |= vk::ImageAspectFlagBits::eDepth;

		if (clearStencil)
			aspect_mask |= vk::ImageAspectFlagBits::eStencil;

		auto attachment = vk::ClearAttachment()
			.setAspectMask(aspect_mask)
			.setColorAttachment(0)
			.setClearValue(clear_value);

		getCurrentFrame().mCommandBuffer.clearAttachments({attachment}, {clear_rect});
	}
}

void DriverVk::draw(uint32_t vertex_count, uint32_t vertex_offset, uint32_t instance_count) {
	EnsureGraphicsState(false);
	getCurrentFrame().mCommandBuffer.draw(vertex_count, instance_count, vertex_offset, 0);
}

void DriverVk::drawIndexed(uint32_t index_count, uint32_t index_offset, uint32_t instance_count) {
	EnsureGraphicsState(true);
	getCurrentFrame().mCommandBuffer.drawIndexed(index_count, instance_count, index_offset, 0, 0);
}

void DriverVk::setClearColor(const MATH::Vector4f& color) {
	mClearColor = color;
}

void DriverVk::setClearColor(float r, float g, float b, float a) {
	mClearColor = {r, g, b, a};
}


void DriverVk::submit() {
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *getCurrentFrame().mCommandBuffer);
	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}
	end();

	const auto& render_complete_semaphore = getCurrentFrame().mRenderCompleteSemaphore;

	vk::PresentInfoKHR presentInfo{};
	presentInfo.setWaitSemaphores(*render_complete_semaphore);
	presentInfo.setSwapchains(*getCurrentSwapchainContext().swapchain);
	presentInfo.setImageIndices(mFrameIndex);

	auto present_result = mQueue.presentKHR(presentInfo);

	mSemaphoreIndex = (mSemaphoreIndex + 1) % getCurrentSwapchainContext().frames.size();


	wait();
	nextFrame();
	begin();
}

void DriverVk::resetViewport() {
	setDirty(Dirty::VIEWPORT);
	mViewport = std::nullopt;
}

void DriverVk::resetScissor() {
	setDirty(Dirty::SCISSOR);
	mScissor = std::nullopt;
}

void DriverVk::resetBlending() {
	setDirty(Dirty::BLENDING);
	mBlendMode = std::nullopt;
}

void DriverVk::resetDepth() {
	setDirty(Dirty::DEPTH);
	mDepthMode = std::nullopt;
}

void DriverVk::resetStencil() {
	setDirty(Dirty::STENCIL);
	mStencilMode = std::nullopt;
}

std::shared_ptr<TextureInterface> DriverVk::createTexture(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureVk::Create(path, allocator, deleter);
}
std::shared_ptr<TextureInterface> DriverVk::createTextureAtlas(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureAtlasVk::CreateAtlas(path, generateMipmap, allocator, deleter);
}
std::shared_ptr<TextureInterface> DriverVk::createTexture(const TextureResource& res, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureVk::Create(res, allocator, deleter);
}
std::shared_ptr<TextureInterface> DriverVk::createTexture(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureVk::Create(res, fileData, allocator, deleter);
}
std::shared_ptr<TextureInterface> DriverVk::createTextureAtlas(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureAtlasVk::CreateAtlasFromResource(res, fileData, allocator, deleter);
}
std::shared_ptr<TextureInterface> DriverVk::createTexture(const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	// TextureLoader::CreateFromFile() goes through this overload. It used to return nullptr, so every
	// file texture silently fell back to the default (pink) texture.
	TextureResource res;
	res.path = name;
	res.useMipmap = generateMipmap;
	res.pathTexture.push_back(name);
	return TextureVk::Create(res, std::vector<std::vector<uint8_t>>{data}, allocator, deleter);
}

std::shared_ptr<ShaderInterface> DriverVk::createShader(const std::string& vertexPath, const std::string& fragmentPath) {
	std::map<ShaderType, std::string> paths;
	paths[ShaderType::VERTEX] = vertexPath;
	paths[ShaderType::FRAGMENT] = fragmentPath;
	return ShaderVk::CreateFromPath(paths);
}

std::shared_ptr<ShaderInterface> DriverVk::createShader(const ShaderResource& res, UTILS::IAllocator* allocator, ShaderDeleter deleter) {
	return AllocateShader<ShaderVk>(allocator, deleter, res);
}

std::shared_ptr<ModelInterface> DriverVk::createModel(const std::string& path, UTILS::IAllocator* allocator, ModelDeleter deleter) {
	ModelDeleter finalDeleter = [deleter](ModelInterface* m) {
		IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::ModelLoader>().unloadResource(m->getPath());
		if (deleter) deleter(m);
	};
	return AllocateModel<ModelVk>(allocator, std::move(finalDeleter), path);
}

std::shared_ptr<MaterialInterface> DriverVk::createMaterial(const MaterialResource& res, UTILS::IAllocator* allocator, MaterialDeleter deleter) {
	MaterialDeleter finalDeleter = [deleter](MaterialInterface* m) {
		IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::MaterialLoader>().unloadResource(m->getPath());
		if (deleter) deleter(m);
	};
	return AllocateMaterial<MaterialVk>(allocator, std::move(finalDeleter), res);
}

std::shared_ptr<FrameBufferInterface> DriverVk::createFrameBuffer(const std::vector<std::shared_ptr<TextureInterface>>& textures, std::shared_ptr<TextureInterface> depth) {
	return std::make_shared<FrameBufferVk>(textures, depth);
}

void DriverVk::draw(const MeshInterface& mesh, PrimitiveMode primitive, uint32_t instances) {
	if (instances > 0) {
		setPrimitiveMode(primitive);
		mesh.bind();
		if (mesh.getIndexCount() > 0) {
			drawIndexed(static_cast<uint32_t>(mesh.getIndexCount()), 0, instances);
		} else {
			draw(static_cast<uint32_t>(mesh.getVertexCount()), 0, instances);
		}
		mesh.unbind();
	}
}

std::shared_ptr<UniformBufferInterface> DriverVk::createUniformBuffer(const void* data, size_t size) {
	return std::make_shared<UniformBufferVk>(const_cast<void*>(data), size);
}

std::shared_ptr<StorageBufferInterface> DriverVk::createStorageBuffer(const void* data, size_t size, size_t stride) {
	return std::make_shared<StorageBufferVk>(const_cast<void*>(data), size, stride);
}

void DriverVk::setFrameBuffer(std::shared_ptr<FrameBufferInterface> frameBuffer) {
	mCurrentState.mFrameBuffer = std::static_pointer_cast<FrameBufferVk>(frameBuffer);
}

void DriverVk::resetFrameBuffer() {
	mCurrentState.mFrameBuffer = nullptr;
}

#endif

