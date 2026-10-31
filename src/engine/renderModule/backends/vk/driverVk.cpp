#include "utilsModule/memory/resourceAllocator.h"

#include "captureModule/screenshot.h"
#include "driverVk.h"
#include "utilsModule/time/loadTimer.h"
#include "vertexFormatsVk.h"
#ifdef IKIGAI_USE_STREAMLINE
#include "../streamlineRuntime.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan_win32.h>
#endif
#include "vulkanLoader.h"
#if defined(__APPLE__)
#include <dlfcn.h>
#endif
// #include "Render/vk/raytracing/dw/include/macros.h"
#ifdef VULKAN_BACKEND
#include <string>
#include <string_view>
#include <unordered_set>

#include "backends/imgui_impl_vulkan.h"
#include "renderModule/backends/interface/storageBufferInterface.h"
#include "utilsModule/imguiHelper/imguiBackend/imguiBackend.h"
#include "utilsModule/log/loggerDefine.h"

// #include "raytracing/dw/include/extensions_vk.h"

#include <algorithm>
#include <array>
#include <assimp/Importer.hpp>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

#include "frameBufferVk.h"
#include "materialVk.h"
#include "modelVk.h"
#include "resourceModule/materialManager.h"
#include "resourceModule/modelManager.h"
#include "resourceModule/serviceManager.h"
#include "shaderVk.h"
#include "storageBufferVk.h"
#include "textureVk.h"
#include "uniformBufferVk.h"
#include <../../3rd/imgui/imgui/imgui.h>
#include <assimp/postprocess.h>
// #include "../../GUI.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <windowModule/window/window.h>

using namespace IKIGAI;
using namespace IKIGAI::RENDER;

#if defined(DEBUG) || defined(_DEBUG)
VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes,
	VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData, void* /*pUserData*/) {
#if !defined(NDEBUG)
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
		if (msg.find("does not conform to naming standard") != std::string_view::npos
			|| msg.find("because it is a duplicate of") != std::string_view::npos) {
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
			std::cerr << "\t\t\t" << "objectType   = " << vk::to_string(static_cast<vk::ObjectType>(pCallbackData->pObjects[i].objectType)) << "\n";
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
	{ PixelFormat::R_FLOAT, vk::Format::eR32Sfloat },
	{ PixelFormat::RG_FLOAT, vk::Format::eR32G32Sfloat },
	{ PixelFormat::RGB_FLOAT, vk::Format::eR32G32B32Sfloat },
	{ PixelFormat::RGBA_FLOAT, vk::Format::eR32G32B32A32Sfloat },
	{ PixelFormat::R_INT, vk::Format::eR8Unorm },
	{ PixelFormat::RG_INT, vk::Format::eR8G8Unorm },
	{ PixelFormat::RGB_INT, vk::Format::eR8G8B8Unorm },
	{ PixelFormat::RGBA_INT, vk::Format::eR8G8B8A8Unorm },
	{ PixelFormat::R32_INT, vk::Format::eR32Sint },
	{ PixelFormat::RG32_INT, vk::Format::eR32G32Sint },
	{ PixelFormat::RGB32_INT, vk::Format::eR32G32B32Sint },
	{ PixelFormat::RGBA32_INT, vk::Format::eR32G32B32A32Sint },
	{ PixelFormat::BGRA_INT, vk::Format::eB8G8R8A8Unorm },
	{ PixelFormat::DEPTH_24_UNORM_STENCIL_8_UINT, vk::Format::eD24UnormS8Uint },
	{ PixelFormat::DEPTH32_FLOAT, vk::Format::eD32Sfloat },
	{ PixelFormat::DEPTH_32_FLOAT_STENCIL_8_UINT, vk::Format::eD32SfloatS8Uint },
};

std::string DriverVk::State::getName() {
	return std::to_string(mShader->getId()) + ":" + (mFrameBuffer ? std::to_string(mFrameBuffer->getId()) : "screen");
}

DriverVk::VolkInitializer::VolkInitializer() {
#ifdef IKIGAI_USE_STREAMLINE
	try {
		Runtime = AcquireStreamline(sl::RenderAPI::eVulkan);
	} catch (const std::exception& e) {
		std::cerr << "STREAMLINE_UNAVAILABLE " << e.what() << '\n';
	}
#endif
#if defined(__APPLE__)
	if (const char* path = FindVulkanLoaderPath()) {
		void* module = dlopen(path, RTLD_NOW | RTLD_LOCAL);
		auto* getInstanceProcAddr = module ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(module, "vkGetInstanceProcAddr")) : nullptr;
		if (getInstanceProcAddr) {
			volkInitializeCustom(getInstanceProcAddr);
			return;
		}
	}
#endif
	if (volkInitialize() != VK_SUCCESS) {
		throw std::runtime_error("Failed to initialize volk. On macOS install the loader and MoltenVK: brew install vulkan-loader molten-vk");
	}
}

DriverVk::DriverVk() {
#ifdef IKIGAI_USE_STREAMLINE
	mDlssRuntime = mVolkInit.Runtime;
#endif
	UtilityVk::mDriver = this;
	DriverVk::init();
}

DriverVk::~DriverVk() {
#if IKIGAI_ENABLE_MEMORY_TRACKING
	if (mMemorySampler) {
		mMemorySampler->disable();
	}
#endif
	if (working) {
		DriverVk::end();
	}
	if (*mDevice) {
		mDevice.waitIdle();
	}
	wait();

	mShuttingDown = true;
	mFrameGeneration.reset();
#ifdef IKIGAI_USE_STREAMLINE
	for (auto& [_, ctx] : mSwapchains) {
		if (ctx.sdkDestroy) {
			mCurrentState.mFrameBuffer.reset();
			for (auto& frame : ctx.frames) {
				frame.mFrameBuffer.reset();
			}
			for (auto& frame : ctx.frames) {
				frame.destroyDeferred.clear();
			}
			ctx.frames.clear();
			ctx.sdkDestroy();
			ctx.sdkDestroy = {};
		}
		if (ctx.sdkDestroySurface && *ctx.surface) {
			mCurrentState.mFrameBuffer.reset();
			for (auto& frame : ctx.frames) {
				frame.mFrameBuffer.reset();
			}
			for (auto& frame : ctx.frames) {
				frame.destroyDeferred.clear();
			}
			ctx.frames.clear();
			ctx.swapchain = nullptr;
			auto surface = ctx.surface.release();
			ctx.sdkDestroySurface(surface);
			ctx.sdkDestroySurface = {};
		}
	}
	if (mDlssRuntime) {
		std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->close();
	}
#endif
	mVertexBuffer.reset();
	mBindlessFrameResources.clear();
	mBindlessTables.clear();
	mVertexStreams.clear();
	mIndexBuffer.reset();
	mUniformBuffers.clear();
	mStorageBuffers.clear();
	mTextures.clear();
	mCurrentState = {};
	mStates.clear();
#if IKIGAI_ENABLE_MEMORY_TRACKING
	mGpuPipelines.clear();
#endif
	mRayTracingFrameResources.clear();
	for (auto& [_, ctx] : mSwapchains) {
		for (auto& frame : ctx.frames) {
			frame.destroyDeferred.clear();
		}
	}
	mSwapchains.clear();
	mDlssRuntime.reset();

#if defined(DEBUG) || defined(_DEBUG)
	if (mAllocator) {
		VmaTotalStatistics stats {};
		vmaCalculateStatistics(mAllocator, &stats);
		std::cerr << "VMA shutdown: allocations=" << stats.total.statistics.allocationCount << " bytes=" << stats.total.statistics.allocationBytes
				  << std::endl;
		LOG_INFO << "VMA shutdown: allocations=" << stats.total.statistics.allocationCount << " bytes=" << stats.total.statistics.allocationBytes;
	}
#endif
	if (mAllocator) {
#if IKIGAI_ENABLE_MEMORY_TRACKING
		const auto snapshot = PROFILER::GpuMemoryProfiler::instance().captureSnapshot();
		const auto live = std::count_if(snapshot.allocations.begin(), snapshot.allocations.end(), [this](const auto& record) {
			return record.info.backend == PROFILER::GpuBackend::Vulkan && record.info.device == reinterpret_cast<uintptr_t>(mAllocator);
		});
		LOG_INFO << "GPU_MEMORY_SHUTDOWN backend=vulkan records=" << live;
		std::cerr << "GPU_MEMORY_SHUTDOWN backend=vulkan records=" << live << std::endl;
#endif
		vmaDestroyAllocator(mAllocator);
		mAllocator = nullptr;
	}
}

vk::raii::DescriptorPool DriverVk::createDrawDescriptorPool() {
	const std::array<vk::DescriptorPoolSize, 4> sizes
		= { { { vk::DescriptorType::eUniformBuffer, 2048 }, { vk::DescriptorType::eStorageBuffer, 1024 },
		{ vk::DescriptorType::eCombinedImageSampler, 4096 }, { vk::DescriptorType::eStorageImage, 4096 } } };
	return mDevice.createDescriptorPool(
		vk::DescriptorPoolCreateInfo().setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet).setMaxSets(512).setPoolSizes(sizes));
}

void DriverVk::init() {
	// auto all_extensions = mContext.enumerateInstanceExtensionProperties();
	//
	// for (auto extension : all_extensions) {
	//	//	std::cout << extension.extensionName << std::endl;
	// }
	//
	// auto all_layers = mContext.enumerateInstanceLayerProperties();
	//
	// for (auto layer : all_layers) {
	//	//	std::cout << layer.layerName << std::endl;
	// }
	auto& win = RESOURCES::ServiceManager::Get<WINDOW::Window>();
	bool enableValidation = false;
#if defined(DEBUG) || defined(_DEBUG)
	std::vector validationLayers = { "VK_LAYER_KHRONOS_validation" };
	enableValidation = checkValidationLayerSupport(validationLayers);
	if (!enableValidation) {
		std::cerr << "VK_LAYER_KHRONOS_validation is not installed; continuing without it. "
				  << "brew install vulkan-validationlayers\n";
	}
#endif

	//	auto extensions = {
	//		VK_KHR_SURFACE_EXTENSION_NAME,
	// #if defined(DEBUG) || defined(_DEBUG)
	//		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
	// #endif
	//	};

	auto extensions = win.getSDLVulkanExtentions();
	extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
	configureUpscalerInstanceExtensions(extensions);
	if (enableValidation) {
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}
	// MoltenVK is a portability driver. The loader hides it unless this extension
	// and VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR are both set.
	const bool portabilityEnumeration = checkInstanceExtensionSupport({ VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME });
	if (portabilityEnumeration) {
		extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
	}
	if (!checkInstanceExtensionSupport(extensions)) {
		throw std::runtime_error("VkInstance doesn't support the required extensions");
	}

	// auto version = mContext.enumerateInstanceVersion();
	// auto major_version = VK_API_VERSION_MAJOR(version);
	// auto minor_version = VK_API_VERSION_MINOR(version);
	// auto patch_version = VK_API_VERSION_PATCH(version);

	// std::cout << "available vulkan version: " << major_version << "." << minor_version << std::endl;

	vk::ApplicationInfo applicationInfo {};
	applicationInfo.setApiVersion(VK_API_VERSION_1_3);

	vk::InstanceCreateInfo instanceInfo {};
	instanceInfo.setPEnabledExtensionNames(extensions);
	instanceInfo.setPApplicationInfo(&applicationInfo);
	if (portabilityEnumeration) {
		instanceInfo.setFlags(vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR);
	}

#if defined(DEBUG) || defined(_DEBUG)
	if (enableValidation) {
		instanceInfo.setPEnabledLayerNames(validationLayers);
	}

	vk::DebugUtilsMessengerCreateInfoEXT debugMessengerInfo {};
	debugMessengerInfo.setMessageSeverity(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
	debugMessengerInfo.setMessageType(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance
		| vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
	debugMessengerInfo.setPfnUserCallback(&DebugCallback);

	auto features = { //	vk::ValidationFeatureEnableEXT::eBestPractices,
		// DebugPrintf is disabled: shaders don't use debugPrintfEXT, and enabling it makes the validation layer
		// emit "DebugPrintf logs to the Information severity" and several "Adding Vk...Features to pNext" warnings.
		//	vk::ValidationFeatureEnableEXT::eDebugPrintf,
		//	vk::ValidationFeatureEnableEXT::eGpuAssisted,
		//	vk::ValidationFeatureEnableEXT::eGpuAssistedReserveBindingSlot,
		vk::ValidationFeatureEnableEXT::eSynchronizationValidation
	};

	vk::ValidationFeaturesEXT validationFeatures {};
	validationFeatures.setEnabledValidationFeatures(features);

#endif

	const uint32_t apiVersions[] = {
		VK_API_VERSION_1_3,
		VK_API_VERSION_1_2,
		VK_API_VERSION_1_1,
	};
	bool instanceCreated = false;
	for (const uint32_t apiVersion : apiVersions) {
		applicationInfo.setApiVersion(apiVersion);
		try {
#if defined(DEBUG) || defined(_DEBUG)
			if (enableValidation) {
				auto structureChain = vk::StructureChain<vk::InstanceCreateInfo, vk::DebugUtilsMessengerCreateInfoEXT, vk::ValidationFeaturesEXT>(
					instanceInfo, debugMessengerInfo, validationFeatures);
				mInstance = mContext.createInstance(structureChain.get<vk::InstanceCreateInfo>());
			}
			else
#endif
			{
				mInstance = mContext.createInstance(instanceInfo);
			}
			instanceCreated = true;
			std::cout << "Vulkan instance API " << VK_API_VERSION_MAJOR(apiVersion) << "." << VK_API_VERSION_MINOR(apiVersion) << "\n";
			break;
		} catch (const vk::IncompatibleDriverError&) {
			std::cerr << "vkCreateInstance rejected Vulkan " << VK_API_VERSION_MAJOR(apiVersion) << "." << VK_API_VERSION_MINOR(apiVersion) << "\n";
		}
	}
	if (!instanceCreated) {
		throw std::runtime_error(
			"vkCreateInstance failed: no compatible Vulkan driver. On macOS install MoltenVK (brew install molten-vk vulkan-loader).");
	}
	volkLoadInstance(*mInstance);

#if defined(DEBUG) || defined(_DEBUG)
	if (enableValidation) {
		mDebugMessenger = mInstance.createDebugUtilsMessengerEXT(debugMessengerInfo);
	}
#endif

	auto devices = mInstance.enumeratePhysicalDevices();
	size_t device_index = 0;
	auto preferred_device_type
		= vk::PhysicalDeviceType::eDiscreteGpu; // TODO: adapter == Adapter::HighPerformance ? vk::PhysicalDeviceType::eDiscreteGpu :
												// vk::PhysicalDeviceType::eIntegratedGpu;
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
#if IKIGAI_ENABLE_MEMORY_TRACKING
	const bool memoryBudget = std::any_of(all_device_extensions.begin(), all_device_extensions.end(),
		[](const auto& extension) { return std::string_view(extension.extensionName) == VK_EXT_MEMORY_BUDGET_EXTENSION_NAME; });
	if (memoryBudget) {
		device_extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
	}
#endif
	for (const auto& device_extension : all_device_extensions) {
		if (std::string_view(device_extension.extensionName) == "VK_KHR_portability_subset") {
			device_extensions.push_back("VK_KHR_portability_subset");
			break;
		}
	}

	// if (features.contains(Feature::Raytracing)) {
	//	device_extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
	//	device_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	//	device_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	// }

	std::vector<float> queuePriority(1, 1.f);
#if defined(IKIGAI_USE_FSR_FRAME_GENERATION) || defined(IKIGAI_USE_STREAMLINE)
	if (mPhysicalDevice.getQueueFamilyProperties().at(mQueueFamilyIndex).queueCount >= 4) {
		queuePriority.assign(4, 1.f);
	}
#endif

	vk::DeviceQueueCreateInfo queueInfo {};
	queueInfo.setQueueFamilyIndex(mQueueFamilyIndex);
	queueInfo.setQueuePriorities(queuePriority);

	auto deviceFeatures
		= mPhysicalDevice
			  .getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT>();
	configureRayTracing(device_extensions);
	VkPhysicalDeviceVulkan12Features bindlessFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	VkPhysicalDeviceFeatures2 bindlessQuery { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	bindlessQuery.pNext = &bindlessFeatures;
	vkGetPhysicalDeviceFeatures2(*mPhysicalDevice, &bindlessQuery);
	mRTCoreFeatures.runtimeDescriptorArray = bindlessFeatures.runtimeDescriptorArray;
	mRTCoreFeatures.shaderSampledImageArrayNonUniformIndexing = bindlessFeatures.shaderSampledImageArrayNonUniformIndexing;
	mRTCoreFeatures.shaderStorageBufferArrayNonUniformIndexing = bindlessFeatures.shaderStorageBufferArrayNonUniformIndexing;
	const auto bindlessLimits = mPhysicalDevice.getProperties().limits;
	mBindlessCapabilities.sampledTextures = bindlessFeatures.shaderSampledImageArrayNonUniformIndexing;
	mBindlessCapabilities.storageBuffers = bindlessFeatures.shaderStorageBufferArrayNonUniformIndexing;
	mBindlessCapabilities.nonUniformIndexing = mBindlessCapabilities.sampledTextures;
	mBindlessCapabilities.runtimeArrays = bindlessFeatures.runtimeDescriptorArray;
	mBindlessCapabilities.maxSampledTextures = std::min({ bindlessLimits.maxPerStageDescriptorSamplers,
		bindlessLimits.maxPerStageDescriptorSampledImages, bindlessLimits.maxDescriptorSetSamplers, bindlessLimits.maxDescriptorSetSampledImages });
	mBindlessCapabilities.maxStorageBuffers
		= std::min(bindlessLimits.maxPerStageDescriptorStorageBuffers, bindlessLimits.maxDescriptorSetStorageBuffers);
	configureGpuFeatures(device_extensions);
	configureUpscalerExtensions(device_extensions);
	configureStreamlineExtensions(device_extensions, false);
	VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT fgAtomics { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT };
#ifdef IKIGAI_USE_FSR_FRAME_GENERATION
	const auto availableFgExtensions = mPhysicalDevice.enumerateDeviceExtensionProperties();
	if (std::any_of(availableFgExtensions.begin(), availableFgExtensions.end(),
			[](const auto& e) { return std::string_view(e.extensionName) == VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME; })) {
		VkPhysicalDeviceFeatures2 fgSupport { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		fgSupport.pNext = &fgAtomics;
		vkGetPhysicalDeviceFeatures2(*mPhysicalDevice, &fgSupport);
		fgAtomics.sparseImageInt64Atomics = VK_FALSE;
		if (fgAtomics.shaderImageInt64Atomics) {
			device_extensions.push_back(VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME);
			auto* tail = reinterpret_cast<VkBaseOutStructure*>(&mRTCoreFeatures);
			while (tail->pNext) {
				tail = tail->pNext;
			}
			tail->pNext = reinterpret_cast<VkBaseOutStructure*>(&fgAtomics);
		}
	}
#endif
	VkPhysicalDeviceOpticalFlowFeaturesNV opticalFlow { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV };
	std::vector<vk::DeviceQueueCreateInfo> queueInfos { queueInfo };
	const float opticalPriority = 1.f;
#ifdef IKIGAI_USE_DLSS_FRAME_GENERATION
	if (mDlssRuntime && std::any_of(all_device_extensions.begin(), all_device_extensions.end(), [](const auto& e) {
			return std::string_view(e.extensionName) == VK_NV_OPTICAL_FLOW_EXTENSION_NAME;
		})) {
		const auto families = mPhysicalDevice.getQueueFamilyProperties();
		for (uint32_t i = 0; i < families.size(); ++i) {
			if ((families[i].queueFlags & vk::QueueFlagBits::eOpticalFlowNV)
				&& !(families[i].queueFlags & (vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute))) {
				VkPhysicalDeviceFeatures2 support { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
				support.pNext = &opticalFlow;
				vkGetPhysicalDeviceFeatures2(*mPhysicalDevice, &support);
				if (opticalFlow.opticalFlow) {
					mOpticalFlowFamily = i;
					if (std::none_of(device_extensions.begin(), device_extensions.end(),
							[](auto name) { return std::string_view(name) == VK_NV_OPTICAL_FLOW_EXTENSION_NAME; })) {
						device_extensions.push_back(VK_NV_OPTICAL_FLOW_EXTENSION_NAME);
					}
					vk::DeviceQueueCreateInfo extra {};
					extra.setQueueFamilyIndex(i).setQueueCount(1).setPQueuePriorities(&opticalPriority);
					queueInfos.push_back(extra);
					auto* tail = reinterpret_cast<VkBaseOutStructure*>(&mRTCoreFeatures);
					while (tail->pNext) {
						tail = tail->pNext;
					}
					tail->pNext = reinterpret_cast<VkBaseOutStructure*>(&opticalFlow);
				}
				break;
			}
		}
	}
#endif
	if (mRayTracingCapabilities.AccelerationStructure || mRTCoreFeatures.shaderFloat16 || mRTCoreFeatures.timelineSemaphore
		|| mRTCoreFeatures.shaderSampledImageArrayNonUniformIndexing || mRTCoreFeatures.shaderStorageBufferArrayNonUniformIndexing
		|| mRTCoreFeatures.drawIndirectCount || mRTCoreFeatures.shaderBufferInt64Atomics) {
		deviceFeatures.get<vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT>().pNext = &mRTCoreFeatures;
	}

	vk::DeviceCreateInfo deviceInfo {};
	deviceInfo.setQueueCreateInfos(queueInfos);
	deviceInfo.setPEnabledExtensionNames(device_extensions);

	// if (features.contains(Feature::Raytracing))
	//	deviceInfo.setPNext(&raytracing_device_features.get<vk::PhysicalDeviceFeatures2>());
	// else
	deviceInfo.setPNext(&deviceFeatures.get<vk::PhysicalDeviceFeatures2>());

	mDevice = mPhysicalDevice.createDevice(deviceInfo);
	volkLoadDevice(*mDevice);

	mDescriptorPool = createDrawDescriptorPool();

	{
		VmaVulkanFunctions vulkanFunctions {};
		vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
		vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

		VmaAllocatorCreateInfo allocatorCreateInfo {};
		allocatorCreateInfo.physicalDevice = *mPhysicalDevice;
		allocatorCreateInfo.device = *mDevice;
		allocatorCreateInfo.instance = *mInstance;
		allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_3;
		allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;
#if IKIGAI_ENABLE_MEMORY_TRACKING
		if (memoryBudget) {
			allocatorCreateInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
		}
#endif
		if (mRayTracingCapabilities.AccelerationStructure) {
			allocatorCreateInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
		}

		const VkResult allocatorResult = vmaCreateAllocator(&allocatorCreateInfo, &mAllocator);
		if (allocatorResult != VK_SUCCESS) {
			throw std::runtime_error("vmaCreateAllocator failed");
		}

#if IKIGAI_ENABLE_MEMORY_TRACKING
		try {
			const auto allocator = mAllocator;
			mMemorySampler = std::make_shared<PROFILER::GpuMemorySampler>([allocator, memoryBudget, sampleIndex = uint32_t { 0 }]() mutable {
				// VMA refreshes the driver budget here; advance its cache only on demand.
				vmaSetCurrentFrameIndex(allocator, ++sampleIndex);
				const VkPhysicalDeviceMemoryProperties* properties = nullptr;
				vmaGetMemoryProperties(allocator, &properties);
				VmaBudget budgets[VK_MAX_MEMORY_HEAPS] {};
				vmaGetHeapBudgets(allocator, budgets);
				std::vector<PROFILER::GpuHeapSnapshot> result;
				result.reserve(properties->memoryHeapCount);
				for (uint32_t i = 0; i < properties->memoryHeapCount; ++i) {
					PROFILER::GpuHeapSnapshot heap;
					heap.backend = PROFILER::GpuBackend::Vulkan;
					heap.device = reinterpret_cast<uintptr_t>(allocator);
					heap.heap = i;
					heap.deviceLocal = (properties->memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
					heap.capacityBytes = properties->memoryHeaps[i].size;
					heap.allocationBytes = budgets[i].statistics.allocationBytes;
					heap.reservedBytes = budgets[i].statistics.blockBytes;
					heap.allocationCount = budgets[i].statistics.allocationCount;
					heap.processUsageBytes = budgets[i].usage;
					heap.budgetBytes = budgets[i].budget;
					heap.driverBudget = memoryBudget;
					result.push_back(heap);
				}
				return result;
			});
			PROFILER::GpuMemoryProfiler::instance().registerSampler(mMemorySampler);
		} catch (...) {
			mMemorySampler.reset();
		}
#endif

#if defined(DEBUG) || defined(_DEBUG)
		VmaTotalStatistics stats {};
		vmaCalculateStatistics(mAllocator, &stats);
		LOG_INFO << "VMA init: allocations=" << stats.total.statistics.allocationCount << " bytes=" << stats.total.statistics.allocationBytes;
#endif
	}

	mQueue = mDevice.getQueue(mQueueFamilyIndex, 0);
	if (queuePriority.size() >= 4) {
		for (unsigned i = 0; i < 3; ++i) {
			vkGetDeviceQueue(*mDevice, mQueueFamilyIndex, i + 1, &mPresentationQueues[i]);
		}
	}
	bindStreamlineDevice();

	// #if defined(SKYGFX_PLATFORM_WINDOWS)
	//	auto surface_info = vk::Win32SurfaceCreateInfoKHR()
	//		.setHwnd((HWND)window);
	// #elif defined(SKYGFX_PLATFORM_MACOS)
	//	auto surface_info = vk::MacOSSurfaceCreateInfoMVK()
	//		.setPView(window);
	// #elif defined(SKYGFX_PLATFORM_IOS)
	//	auto surface_info = vk::IOSSurfaceCreateInfoMVK()
	//		.setPView(window);
	// #endif

	// TODO: add subscription for it

	mWidth = win.getSize().x;
	mHeight = win.getSize().y;

	// mSurface = vk::raii::SurfaceKHR(mInstance, surfaceInfo);
	mCurrentWindowID = win.getId();
	VkSurfaceKHR surface = VK_NULL_HANDLE;
#ifdef IKIGAI_USE_STREAMLINE
	std::function<void(VkSurfaceKHR)> destroySurface;
	if (mDlssRuntime) {
		auto runtime = std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime);
		auto get = runtime->symbol<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
		auto create = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(get(*mInstance, "vkCreateWin32SurfaceKHR"));
		auto destroy = reinterpret_cast<PFN_vkDestroySurfaceKHR>(get(*mInstance, "vkDestroySurfaceKHR"));
		VkWin32SurfaceCreateInfoKHR info { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
		info.hinstance = GetModuleHandleW(nullptr);
		info.hwnd
			= static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(win.getSDLWindow()), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
		if (!create || !destroy || create(*mInstance, &info, nullptr, &surface) != VK_SUCCESS) {
			throw std::runtime_error("Streamline Win32 surface creation failed");
		}
		destroySurface = [this, runtime, destroy](VkSurfaceKHR s) { destroy(*mInstance, s, nullptr); };
	}
	else
#endif
		surface = win.createVulkanSurface(*mInstance);

	SwapchainContextVk ctx;
	ctx.surface = vk::raii::SurfaceKHR(mInstance, surface);
#ifdef IKIGAI_USE_STREAMLINE
	ctx.sdkDestroySurface = std::move(destroySurface);
#endif
	mSwapchains[mCurrentWindowID] = std::move(ctx);

	auto formats = mPhysicalDevice.getSurfaceFormatsKHR(*mSwapchains[mCurrentWindowID].surface);

	if ((formats.size() == 1) && (formats.at(0).format == vk::Format::eUndefined)) {
		mSurfaceFormat = { vk::Format::eB8G8R8A8Unorm, formats.at(0).colorSpace };
	}
	else {
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

	vk::CommandPoolCreateInfo commandPoolInfo {};
	commandPoolInfo.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
	commandPoolInfo.setQueueFamilyIndex(mQueueFamilyIndex);

	mCommandPool = mDevice.createCommandPool(commandPoolInfo);

	// mCurrentState.color_attachment_formats = {mSurfaceFormat.format};
	// mCurrentState.depth_stencil_format = ContextVK::DefaultDepthStencilFormat;

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
			if (strcmp(proposedExt, extension.extensionName) == 0) {
				hasExtension = true;
				break;
			}
		}

		if (!hasExtension) {
			std::cerr << "Missing Vulkan extension: " << proposedExt << std::endl;
			return false;
		}
	}
	return true;
}

void DriverVk::begin() {
	working = true;
	UniformBufferVk::BeginFrame();
	mRayTracingFrameResources.clear();
	// Previous frame's fence has already signaled (submit waits before begin).
	mFrameDescriptorSets.clear();
	mBindlessFrameResources.clear();
	mDescriptorPoolIndex = 0;

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

	// gContext->pipeline_state_dirty = true;
	// gContext->topology_dirty = true;
	// gContext->cull_mode_dirty = true;
	// gContext->front_face_dirty = true;
	// gContext->blend_mode_dirty = true;
	// gContext->depth_mode_dirty = true;
	// gContext->stencil_mode_dirty = true;

	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	getCurrentFrame().mCommandBuffer.begin(beginInfo);

	auto tex = std::static_pointer_cast<TextureVk>(getCurrentFrame().mFrameBuffer->getTextures()[0]);
	// tex->setState(getCurrentFrame().mCommandBuffer, vk::ImageLayout::eColorAttachmentOptimal);
	// TODO:
	UtilityVk::SetImageMemoryBarrier(getCurrentFrame().mCommandBuffer, tex->mImagePtr, vk::ImageAspectFlagBits::eColor, tex->mCurrentState,
		vk::ImageLayout::eColorAttachmentOptimal);
	tex->mCurrentState = vk::ImageLayout::eColorAttachmentOptimal;
}

void DriverVk::end() {
	// gContext->working = false;

	deactivateRenderPass();

	auto tex = std::static_pointer_cast<TextureVk>(getCurrentFrame().mFrameBuffer->getTextures()[0]);
	const auto& presenting = getCurrentSwapchainContext();
	// FSR exposes virtual backbuffers consumed as sampled images by its
	// presenter; Streamline and native WSI expose presentation images.
	const auto finalLayout = presenting.sdkSwapchain
		? (presenting.streamline ? vk::ImageLayout::eTransferSrcOptimal : vk::ImageLayout::eShaderReadOnlyOptimal)
		: vk::ImageLayout::ePresentSrcKHR;
	tex->setState(getCurrentFrame().mCommandBuffer, finalLayout);

	// UtilityVk::SetImageMemoryBarrier(getCurrentFrame().mCommandBuffer, tex->mImagePtr, vk::ImageAspectFlagBits::eColor,
	// vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR);

	getCurrentFrame().mCommandBuffer.end();

	const auto& frame = getCurrentFrame();

	mDevice.resetFences({ *frame.fence });

	auto wait_dst_stage_mask = vk::PipelineStageFlags { vk::PipelineStageFlagBits::eAllCommands };

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

	auto pipeline_input_assembly_state_create_info = vk::PipelineInputAssemblyStateCreateInfo().setTopology(topology);

	vk::PipelineTessellationStateCreateInfo pipeline_tessellation_state_create_info = vk::PipelineTessellationStateCreateInfo().setPatchControlPoints(
		3); // Default to 3, but this can be dynamic via VK_EXT_extended_dynamic_state3 if needed

	auto pipeline_viewport_state_create_info = vk::PipelineViewportStateCreateInfo().setViewportCount(1).setScissorCount(1);

	auto pipeline_rasterization_state_create_info = vk::PipelineRasterizationStateCreateInfo().setPolygonMode(vk::PolygonMode::eFill);

	auto pipeline_multisample_state_create_info = vk::PipelineMultisampleStateCreateInfo().setRasterizationSamples(vk::SampleCountFlagBits::e1);

	auto pipeline_depth_stencil_state_create_info = vk::PipelineDepthStencilStateCreateInfo();

	auto fb = pipeline_state.mFrameBuffer ? pipeline_state.mFrameBuffer : getCurrentFrame().mFrameBuffer;

	// MoltenVK indexes pAttachments while building fragment outputs. A count without
	// the array crashes inside MVKGraphicsPipeline::addFragmentOutputToPipeline.
	std::vector<vk::PipelineColorBlendAttachmentState> blendAttachments(fb->getTextures().size());
	for (auto& attachment : blendAttachments) {
		attachment.blendEnable = VK_FALSE;
		attachment.colorWriteMask
			= vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
	}
	auto pipeline_color_blend_state_create_info = vk::PipelineColorBlendStateCreateInfo().setAttachments(blendAttachments);

	std::vector<vk::VertexInputBindingDescription> vertex_input_binding_descriptions;
	std::vector<vk::VertexInputAttributeDescription> vertex_input_attribute_descriptions;

	validateVertexInputs(*pipeline_state.mShader);
	if (pipeline_state.mShader->vertexFetchMode() == VertexFetchMode::Pulling) {
		if (!pipeline_state.mShader->getReflection().mInputParams.empty()) {
			throw std::runtime_error("Pulling shader must not declare vertex attributes");
		}
	}
	else if (mVertexLayout) {
		for (const auto& stream : mVertexLayout->streams) {
			const auto rate = stream.rate == VertexInputRate::PerInstance ? vk::VertexInputRate::eInstance : vk::VertexInputRate::eVertex;
			vertex_input_binding_descriptions.emplace_back(stream.binding, stream.stride, rate);
		}
		for (const auto& attribute : mVertexLayout->attributes) {
			vertex_input_attribute_descriptions.emplace_back(
				attribute.location, attribute.binding, NativeVertexFormat(attribute.format), attribute.offset);
		}
	}
	else {
		vertex_input_binding_descriptions.emplace_back(0, 0, vk::VertexInputRate::eVertex);
		for (const auto& param : pipeline_state.mShader->getReflection().mInputParams) {
			vertex_input_attribute_descriptions.emplace_back(param.mLocation, 0, FormatMap.at(param.mFormat), param.mOffset);
		}
	}

	// TODO: use dynamic state for InputLayout
	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_EXT_vertex_input_dynamic_state.html

	auto pipeline_vertex_input_state_create_info = vk::PipelineVertexInputStateCreateInfo()
													   .setVertexBindingDescriptions(vertex_input_binding_descriptions)
													   .setVertexAttributeDescriptions(vertex_input_attribute_descriptions);

	std::vector<vk::DynamicState> dynamic_states = { vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::ePrimitiveTopology,
		vk::DynamicState::eLineWidth, vk::DynamicState::eCullMode, vk::DynamicState::eFrontFace, vk::DynamicState::eVertexInputBindingStride,
		vk::DynamicState::eDepthTestEnable, vk::DynamicState::eDepthCompareOp, vk::DynamicState::eDepthWriteEnable,
		vk::DynamicState::eColorWriteMaskEXT, vk::DynamicState::eColorBlendEquationEXT, vk::DynamicState::eColorBlendEnableEXT,
		vk::DynamicState::eStencilTestEnable };
	if (pipeline_state.mShader->vertexFetchMode() == VertexFetchMode::Pulling) {
		std::erase(dynamic_states, vk::DynamicState::eVertexInputBindingStride);
	}

	auto pipeline_dynamic_state_create_info = vk::PipelineDynamicStateCreateInfo().setDynamicStates(dynamic_states);

	std::vector<vk::Format> color_attachment_formats;
	for (auto& e : fb->getTextures()) {
		color_attachment_formats.push_back(FormatMap.at(e->getFormat()));
	}
	auto depth_stencil_format = fb->getDepth() ? FormatMap.at(fb->getDepth()->getFormat()) : vk::Format::eUndefined;
	auto pipeline_rendering_create_info
		= vk::PipelineRenderingCreateInfo()
			  .setColorAttachmentFormats(color_attachment_formats)
			  .setDepthAttachmentFormat(depth_stencil_format)
			  .setStencilAttachmentFormat(
				  fb->getDepth() && fb->getDepth()->getFormat() != PixelFormat::DEPTH32_FLOAT ? depth_stencil_format : vk::Format::eUndefined);

	auto graphics_pipeline_create_info
		= vk::GraphicsPipelineCreateInfo()
			  .setLayout(*pipeline_state.mShader->mPipelineLayout)
			  .setFlags(vk::PipelineCreateFlagBits())
			  .setStages(pipeline_shader_stage_create_info)
			  .setPVertexInputState(&pipeline_vertex_input_state_create_info)
			  .setPInputAssemblyState(&pipeline_input_assembly_state_create_info)
			  .setPViewportState(&pipeline_viewport_state_create_info)
			  .setPRasterizationState(&pipeline_rasterization_state_create_info)
			  .setPMultisampleState(&pipeline_multisample_state_create_info)
			  .setPDepthStencilState(&pipeline_depth_stencil_state_create_info)
			  .setPColorBlendState(
				  &pipeline_color_blend_state_create_info) // TODO: this can be nullptr
														   // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkGraphicsPipelineCreateInfo.html
			  .setPDynamicState(&pipeline_dynamic_state_create_info)
			  .setRenderPass(nullptr)
			  .setPNext(&pipeline_rendering_create_info);

	if (topology == vk::PrimitiveTopology::ePatchList) {
		graphics_pipeline_create_info.setPTessellationState(&pipeline_tessellation_state_create_info);
	}

	UTILS::LoadTimer timing("graphics-pipeline", pipeline_state.mShader->mPath);
	auto pipeline = mDevice.createGraphicsPipeline(nullptr, graphics_pipeline_create_info);
#if IKIGAI_ENABLE_MEMORY_TRACKING
	const auto handle = PROFILER::GpuHandleValue(static_cast<VkPipeline>(*pipeline));
	try {
		mGpuPipelines.emplace(handle,
			PROFILER::TrackGpuObject(PROFILER::GpuBackend::Vulkan, reinterpret_cast<std::uintptr_t>(static_cast<VkDevice>(*mDevice)), handle,
				PROFILER::GpuResourceKind::Pipeline, "Graphics pipeline"));
	} catch (...) {
		PROFILER::GpuMemoryProfiler::instance().noteDroppedRecord();
	}
#endif
	return pipeline;
}

static vk::IndexType GetIndexTypeFromStride(size_t stride) {
	return stride == 2 ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
}

void DriverVk::EnsureVertexBuffers(vk::raii::CommandBuffer& cmdlist) {
	if (mCurrentState.mShader && mCurrentState.mShader->vertexFetchMode() == VertexFetchMode::Pulling) {
		return;
	}
	if (!isDirty(Dirty::VERTEX_BUFFER)) {
		return;
	}
	clearDirty(Dirty::VERTEX_BUFFER);

	if (mVertexLayout) {
		for (const auto& [binding, buffer] : mVertexStreams) {
			const auto native = std::static_pointer_cast<VertexBufferVk>(buffer);
			const vk::Buffer handle = *native->getBuffer();
			const vk::DeviceSize offset = 0, stride = buffer->getStride();
			cmdlist.bindVertexBuffers2(binding, handle, offset, nullptr, stride);
		}
		return;
	}
	if (!mVertexBuffer || !*mVertexBuffer->getBuffer()) {
		LOG_ERROR << "DriverVk: vertex buffer is not set or empty";
		return;
	}

	std::vector<vk::Buffer> buffers;
	std::vector<vk::DeviceSize> offsets;
	std::vector<vk::DeviceSize> strides;

	// for (auto vertex_buffer : gContext->vertex_buffers) {
	buffers.push_back(*mVertexBuffer->getBuffer());
	offsets.push_back(0);
	strides.push_back(mVertexBuffer->getStride());
	//}

	cmdlist.bindVertexBuffers2(0, buffers, offsets, nullptr, strides);
}

void DriverVk::EnsureIndexBuffer(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::INDEX_BUFFER)) {
		return;
	}
	clearDirty(Dirty::INDEX_BUFFER);

	if (!mIndexBuffer || !*mIndexBuffer->getBuffer()) {
		LOG_ERROR << "DriverVk: index buffer is not set or empty";
		return;
	}

	auto index_type = GetIndexTypeFromStride(mIndexBuffer->getStride());
	cmdlist.bindIndexBuffer(*mIndexBuffer->getBuffer(), 0, index_type);
}

void DriverVk::EnsureTopology(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::PRIMITIVE_MODE)) {
		return;
	}
	clearDirty(Dirty::PRIMITIVE_MODE);

	static const std::unordered_map<PrimitiveMode, vk::PrimitiveTopology> TopologyMap = {
		{ PrimitiveMode::POINTS, vk::PrimitiveTopology::ePointList },
		{ PrimitiveMode::LINES, vk::PrimitiveTopology::eLineList },
		{ PrimitiveMode::LINE_STRIP, vk::PrimitiveTopology::eLineStrip },
		{ PrimitiveMode::TRIANGLES, vk::PrimitiveTopology::eTriangleList },
		{ PrimitiveMode::TRIANGLE_STRIP, vk::PrimitiveTopology::eTriangleStrip },
		{ PrimitiveMode::LINES_ADJACENCY, vk::PrimitiveTopology::eLineListWithAdjacency },
		{ PrimitiveMode::LINE_STRIP_ADJACENCY, vk::PrimitiveTopology::eLineStripWithAdjacency },
		{ PrimitiveMode::TRIANGLES_ADJACENCY, vk::PrimitiveTopology::eTriangleListWithAdjacency },
		{ PrimitiveMode::TRIANGLE_STRIP_ADJACENCY, vk::PrimitiveTopology::eTriangleStripWithAdjacency },
		{ PrimitiveMode::PATCHES, vk::PrimitiveTopology::ePatchList },
	};

	auto topology = TopologyMap.at(mPrimitiveMode);

	// Pipeline with tessellation is always created with ePatchList (see createState),
	// dynamic topology must be the same class, otherwise Vulkan validation fails.
	if (mCurrentState.mShader
		&& (*mCurrentState.mShader->mTessellationControlShaderModule || *mCurrentState.mShader->mTessellationEvaluationShaderModule)) {
		topology = vk::PrimitiveTopology::ePatchList;
	}

	cmdlist.setPrimitiveTopology(topology);
}

void DriverVk::EnsureViewport(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::VIEWPORT)) {
		return;
	}
	clearDirty(Dirty::VIEWPORT);

	auto width = static_cast<float>(mWidth);
	auto height = static_cast<float>(mHeight);

	auto value = mViewport.value_or(Viewport { { 0.0f, 0.0f }, { width, height } });

	auto viewport = vk::Viewport()
						.setX(value.mPosition.x)
						.setY(value.mPosition.y + value.mSize.y)
						.setWidth(value.mSize.x)
						.setHeight(-value.mSize.y)
						.setMinDepth(value.mMinDepth)
						.setMaxDepth(value.mMaxDepth);

	cmdlist.setViewport(0, { viewport });
}

void DriverVk::EnsureScissor(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::SCISSOR)) {
		return;
	}
	clearDirty(Dirty::SCISSOR);

	auto width = static_cast<float>(getBackbufferWidth());
	auto height = static_cast<float>(getBackbufferHeight());

	auto value = mScissor.value_or(Scissor { { 0.0f, 0.0f }, { width, height } });

	auto rect = vk::Rect2D()
					.setOffset({ static_cast<int32_t>(value.mPosition.x), static_cast<int32_t>(value.mPosition.y) })
					.setExtent({ static_cast<uint32_t>(value.mSize.x), static_cast<uint32_t>(value.mSize.y) });

	if (rect.offset.x < 0) {
		rect.extent.width -= rect.offset.x;
		rect.offset.x = 0;
	}

	if (rect.offset.y < 0) {
		rect.extent.height -= rect.offset.y;
		rect.offset.y = 0;
	}

	if (rect.extent.width < 0) {
		rect.extent.width = 0;
	}

	if (rect.extent.height < 0) {
		rect.extent.height = 0;
	}

	cmdlist.setScissor(0, { rect });
}

void DriverVk::EnsureCullMode(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::CULLING)) {
		return;
	}
	clearDirty(Dirty::CULLING);

	const static std::unordered_map<CullFace, vk::CullModeFlags> CullModeMap = {
		{ CullFace::NONE, vk::CullModeFlagBits::eNone },
		{ CullFace::FRONT, vk::CullModeFlagBits::eFront },
		{ CullFace::BACK, vk::CullModeFlagBits::eBack },
	};

	cmdlist.setCullMode(CullModeMap.at(mCullFace));
}

void DriverVk::EnsureFrontFace(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::TRIANGULATION_ORDER)) {
		return;
	}
	clearDirty(Dirty::TRIANGULATION_ORDER);

	// The negative viewport height reverses window-space winding.
	const static std::unordered_map<TriangleOrientation, vk::FrontFace> FrontFaceMap = {
		{ TriangleOrientation::CW, vk::FrontFace::eCounterClockwise },
		{ TriangleOrientation::CCW, vk::FrontFace::eClockwise },
	};

	cmdlist.setFrontFace(FrontFaceMap.at(mTriangleOrientation));
}

void DriverVk::EnsureBlendMode(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::BLENDING)) {
		return;
	}
	clearDirty(Dirty::BLENDING);

	static const std::unordered_map<BlendMode, vk::BlendFactor> BlendFactorMap
		= { { BlendMode::ONE, vk::BlendFactor::eOne }, { BlendMode::ZERO, vk::BlendFactor::eZero },
			  { BlendMode::SRC_COLOR, vk::BlendFactor::eSrcColor }, { BlendMode::ONE_MINUS_SRC_COLOR, vk::BlendFactor::eOneMinusSrcColor },
			  { BlendMode::SRC_ALPHA, vk::BlendFactor::eSrcAlpha }, { BlendMode::ONE_MINUS_SRC_ALPHA, vk::BlendFactor::eOneMinusSrcAlpha },
			  { BlendMode::DST_COLOR, vk::BlendFactor::eDstColor }, { BlendMode::ONE_MINUS_DST_COLOR, vk::BlendFactor::eOneMinusDstColor },
			  { BlendMode::DST_ALPHA, vk::BlendFactor::eDstAlpha }, { BlendMode::ONE_MINUS_DST_ALPHA, vk::BlendFactor::eOneMinusDstAlpha } };

	static const std::unordered_map<BlendFunction, vk::BlendOp> BlendFuncMap = {
		{ BlendFunction::ADD, vk::BlendOp::eAdd },
		{ BlendFunction::SUB, vk::BlendOp::eSubtract },
		{ BlendFunction::REVERT_SUB, vk::BlendOp::eReverseSubtract },
		{ BlendFunction::MIN, vk::BlendOp::eMin },
		{ BlendFunction::MAX, vk::BlendOp::eMax },
	};

	const auto blend_mode = mBlendMode.value_or(Blending(BlendMode::ONE, BlendMode::ZERO));

	auto color_mask = vk::ColorComponentFlags();

	if ((blend_mode.mColorMask & Color::R) != Color::NONE) {
		color_mask |= vk::ColorComponentFlagBits::eR;
	}

	if ((blend_mode.mColorMask & Color::G) != Color::NONE) {
		color_mask |= vk::ColorComponentFlagBits::eG;
	}

	if ((blend_mode.mColorMask & Color::B) != Color::NONE) {
		color_mask |= vk::ColorComponentFlagBits::eB;
	}

	if ((blend_mode.mColorMask & Color::A) != Color::NONE) {
		color_mask |= vk::ColorComponentFlagBits::eA;
	}

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
		blend_enable_array = { mBlendMode.has_value() };
		color_mask_array = { color_mask };
		color_blend_equation_array = { color_blend_equation };
	}
	else {
		for (size_t i = 0; i < mCurrentState.mFrameBuffer->getTextures().size(); i++) {
			blend_enable_array.push_back(mBlendMode.has_value());
			color_mask_array.push_back(color_mask);
			color_blend_equation_array.push_back(color_blend_equation);
		}
	}

	if (!blend_enable_array.empty()) {
		cmdlist.setColorBlendEnableEXT(0, blend_enable_array);
		cmdlist.setColorWriteMaskEXT(0, color_mask_array);
		cmdlist.setColorBlendEquationEXT(0, color_blend_equation_array);
	}
}

const static std::unordered_map<DepthFunction, vk::CompareOp> CompareOpMap = { { DepthFunction::ALWAYS, vk::CompareOp::eAlways },
	{ DepthFunction::NEVER, vk::CompareOp::eNever }, { DepthFunction::LESS, vk::CompareOp::eLess }, { DepthFunction::EQUAL, vk::CompareOp::eEqual },
	{ DepthFunction::NOT_EQUAL, vk::CompareOp::eNotEqual }, { DepthFunction::LESS_EQUAL, vk::CompareOp::eLessOrEqual },
	{ DepthFunction::GREATER, vk::CompareOp::eGreater }, { DepthFunction::GREATER_EQUAL, vk::CompareOp::eGreaterOrEqual } };

void DriverVk::EnsureDepthMode(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::DEPTH)) {
		return;
	}
	clearDirty(Dirty::DEPTH);

	if (mDepthMode) {
		cmdlist.setDepthTestEnable(true);
		cmdlist.setDepthWriteEnable(mDepthMode.value().mWriteMask);
		cmdlist.setDepthCompareOp(CompareOpMap.at(mDepthMode.value().mFunc));
	}
	else {
		cmdlist.setDepthTestEnable(false);
		cmdlist.setDepthWriteEnable(false);
		cmdlist.setDepthCompareOp(vk::CompareOp::eAlways);
	}
}

void DriverVk::EnsureStencilMode(vk::raii::CommandBuffer& cmdlist) {
	if (!isDirty(Dirty::STENCIL)) {
		return;
	}
	clearDirty(Dirty::STENCIL);

	cmdlist.setStencilTestEnable(mStencilMode.has_value());
}

void DriverVk::EnsureGraphicsPipelineState(vk::raii::CommandBuffer& cmdlist) {
	// The pipeline depends on the current shader and framebuffer, which can change between draws
	// (setShader/setFrameBuffer do not raise the dirty flag), so compare by name as well.
	const auto name = mCurrentState.getName() + ":" + vertexLayoutKey();
	if (!isDirty(Dirty::PIPELINE) && name == mBoundPipelineName) {
		return;
	}
	clearDirty(Dirty::PIPELINE);

	if (!mStates.contains(name)) {
		auto pipeline = createState(mCurrentState);
		mStates.insert({ name, std::move(pipeline) });
	}

	const auto& pipeline = mStates.at(name);
	cmdlist.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
	setDirty(Dirty::VERTEX_BUFFER);
	mBoundPipelineName = name;

	graphics_pipeline_ignore_bindings.clear();
}

void DriverVk::EnsureGraphicsDescriptors(vk::raii::CommandBuffer& cmdlist) {
	const auto& pipeline_layout = mCurrentState.mShader->mPipelineLayout;

	// Descriptor bindings are built once by ShaderVk::createPipelineLayout():
	// push constants are excluded (they are not descriptors), all stages and sampler types are covered.
	const auto& required_descriptor_bindings = mCurrentState.mShader->mRequiredDescriptorBindings;

	// auto descriptor_set_layout_create_info = vk::DescriptorSetLayoutCreateInfo()
	//	.setFlags(vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR)
	//	.setBindings(required_descriptor_bindings);
	//
	// auto descriptor_set_layout = gContext->device.createDescriptorSetLayout(descriptor_set_layout_create_info);
	//
	// auto pipeline_layout_create_info = vk::PipelineLayoutCreateInfo()
	//	.setSetLayouts(*descriptor_set_layout);
	//
	// auto pipeline_layout = gContext->device.createPipelineLayout(pipeline_layout_create_info);

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

	if (draw_indexed) {
		EnsureIndexBuffer(cmdlist);
	}

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
	auto wait_result = mDevice.waitForFences({ *frame.fence }, true, UINT64_MAX);
	(void)wait_result;
	// Retiring a TLAS can release BLAS/mesh owners which themselves enqueue
	// buffers for retirement. Do not mutate the vector being destroyed.
	while (!frame.destroyDeferred.empty()) {
		auto retired = std::move(frame.destroyDeferred);
		frame.destroyDeferred.clear();
		retired.clear();
	}
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

	// On a Retina display the surface extent is in pixels. The window size passed in
	// is in points, and a swapchain smaller than the surface only covers part of the layer.
	if (surface_capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
		ctx.width = surface_capabilities.currentExtent.width;
		ctx.height = surface_capabilities.currentExtent.height;
	}
	else {
		ctx.width = std::clamp(width, surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width);
		ctx.height = std::clamp(height, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height);
	}
	mWidth = ctx.width;
	mHeight = ctx.height;

	auto image_extent = vk::Extent2D().setWidth(ctx.width).setHeight(ctx.height);

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

	ctx.lastCreateInfo = swapchain_info;
	ctx.swapchain = mDevice.createSwapchainKHR(swapchain_info);

	auto backbuffers = ctx.swapchain.getImages();
	createSwapchainFrames(windowID, backbuffers);
}

void DriverVk::createSwapchainFrames(unsigned windowID, const std::vector<vk::Image>& backbuffers) {
	auto& ctx = mSwapchains.at(windowID);
	const auto format = mSurfaceFormat.format;
	mCurrentState.mFrameBuffer.reset();

	// Texture destruction enqueues VMA images into the current frame. Keep all
	// queues alive until every old framebuffer has released its depth image.
	for (auto& frame : ctx.frames) {
		frame.mFrameBuffer.reset();
	}
	for (auto& frame : ctx.frames) {
		frame.destroyDeferred.clear();
	}
	ctx.frames.clear();

	for (auto& backbuffer : backbuffers) {
		Frame frame;

		auto fence_info = vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled);

		frame.fence = mDevice.createFence(fence_info);

		frame.mImageAcquiredSemaphore = mDevice.createSemaphore({});
		frame.mRenderCompleteSemaphore = mDevice.createSemaphore({});

		auto command_buffer_allocate_info
			= vk::CommandBufferAllocateInfo().setCommandBufferCount(1).setLevel(vk::CommandBufferLevel::ePrimary).setCommandPool(*mCommandPool);

		auto command_buffers = mDevice.allocateCommandBuffers(command_buffer_allocate_info);

		frame.mCommandBuffer = std::move(command_buffers.at(0));

		auto frameTexture = IKIGAI::MEMORY::MakeShared<TextureVk, IKIGAI::MEMORY::Category::Texture>(ctx.width, ctx.height, format, backbuffer);
		TextureResource depthRes;
		depthRes.width = ctx.width;
		depthRes.height = ctx.height;
		depthRes.depth = 1;
		depthRes.mipMapCount = 1;
		depthRes.useMipmap = false;
		depthRes.texType = TextureType::DEPTH;
		depthRes.pixelType = PixelFormat::DEPTH_32_FLOAT_STENCIL_8_UINT;
		auto frameDepth = IKIGAI::MEMORY::MakeShared<TextureVk, IKIGAI::MEMORY::Category::Texture>(depthRes, std::vector<void*> {});
		frame.mFrameBuffer = std::make_shared<FrameBufferVk>(std::vector<std::shared_ptr<TextureInterface>> { frameTexture }, frameDepth);

		// frame.swapchain_texture = IKIGAI::MEMORY::MakeShared<TextureVk, IKIGAI::MEMORY::Category::Texture>(ctx.width, ctx.height, format,
		// backbuffer); frame.swapchain_target = std::make_shared<RenderTargetVk>(frame.swapchain_texture);

		ctx.frames.push_back(std::move(frame));
	}

	mFrameIndex = 0;
	mSemaphoreIndex = 0;
}

void DriverVk::nextFrame() {
	const auto& image_acquired_semaphore = getCurrentSwapchainContext().frames.at(mSemaphoreIndex).mImageAcquiredSemaphore;

	auto& ctx = getCurrentSwapchainContext();
	if (ctx.sdkAcquire) {
		auto result = ctx.sdkAcquire(*image_acquired_semaphore, &mFrameIndex);
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			throw std::runtime_error("SDK acquire image failed: " + std::to_string(result));
		}
	}
	else {
		mFrameIndex = ctx.swapchain.acquireNextImage(UINT64_MAX, *image_acquired_semaphore).value;
	}
}

uint32_t DriverVk::getBackbufferWidth() {
	return mCurrentState.mFrameBuffer ? mCurrentState.mFrameBuffer->getWidth() : getCurrentSwapchainContext().width;
}

uint32_t DriverVk::getBackbufferHeight() {
	return mCurrentState.mFrameBuffer ? mCurrentState.mFrameBuffer->getHeight() : getCurrentSwapchainContext().height;
}

vk::Format DriverVk::getBackbufferFormat() {
	// TODO: wtf when mrt
	return mCurrentState.mFrameBuffer
		? FormatMap.at(std::static_pointer_cast<TextureVk>(mCurrentState.mFrameBuffer->getTextures().at(0))->getFormat())
		: FormatMap.at(PixelFormat::RGBA_INT); // gContext->surface_format.format;
}

void DriverVk::beginRenderPass() {
	// assert(!gContext->render_pass_active);
	render_pass_active = true;

	auto targets = mCurrentState.mFrameBuffer;

	if (!targets) {
		targets = getCurrentFrame().mFrameBuffer;
	}

	std::vector<vk::RenderingAttachmentInfo> color_attachments;
	std::optional<vk::RenderingAttachmentInfo> depth_stencil_attachment;

	for (auto target : targets->getTextures()) {
		static_cast<TextureVk*>(target.get())->setState(getCurrentFrame().mCommandBuffer, vk::ImageLayout::eGeneral);
		auto color_attachment = vk::RenderingAttachmentInfo()
									.setImageView(target->getType() == TextureType::TEXTURE_CUBE
											? static_cast<TextureVk*>(target.get())->attachmentView(targets->getMip(), targets->getLayer())
											: *static_cast<TextureVk*>(target.get())->mImageView)
									.setImageLayout(vk::ImageLayout::eGeneral)
									.setLoadOp(vk::AttachmentLoadOp::eLoad)
									.setStoreOp(vk::AttachmentStoreOp::eStore);

		color_attachments.push_back(color_attachment);
	}
	if (targets->getDepth()) {
		static_cast<TextureVk*>(targets->getDepth().get())
			->setState(getCurrentFrame().mCommandBuffer, vk::ImageLayout::eDepthStencilAttachmentOptimal);
		depth_stencil_attachment = vk::RenderingAttachmentInfo()
									   .setImageView(*static_cast<TextureVk*>(targets->getDepth().get())->mImageView)
									   .setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
									   .setLoadOp(vk::AttachmentLoadOp::eLoad)
									   .setStoreOp(vk::AttachmentStoreOp::eStore);
	}

	auto width = getBackbufferWidth();
	auto height = getBackbufferHeight();

	auto rendering_info = vk::RenderingInfo().setRenderArea({ { 0, 0 }, { width, height } }).setLayerCount(1).setColorAttachments(color_attachments);

	if (depth_stencil_attachment.has_value()) {
		rendering_info.setPDepthAttachment(&depth_stencil_attachment.value());
		if (targets->getDepth()->getFormat() != PixelFormat::DEPTH32_FLOAT) {
			rendering_info.setPStencilAttachment(&depth_stencil_attachment.value());
		}
	}
	getCurrentFrame().mCommandBuffer.beginRendering(rendering_info);
}

void DriverVk::endRenderPass() {
	// assert(gContext->render_pass_active);
	render_pass_active = false;

	getCurrentFrame().mCommandBuffer.endRendering();
}

void DriverVk::activateRenderPass() {
	if (render_pass_active) {
		return;
	}

	beginRenderPass();
}

void DriverVk::deactivateRenderPass() {
	if (!render_pass_active) {
		return;
	}

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
#if IKIGAI_ENABLE_MEMORY_TRACKING
	std::visit(
		[](auto& resource) {
			if constexpr (requires { resource.markPendingRelease(); }) {
				resource.markPendingRelease();
			}
		},
		object);
#endif
	it->second.frames[mFrameIndex].destroyDeferred.push_back(std::move(object));
}

std::vector<DriverVk::VulkanObject>& DriverVk::getDestroyDeferredObjects() {
	return getCurrentFrame().destroyDeferred;
}

void DriverVk::EnsureMemoryState(const vk::raii::CommandBuffer& cmdbuf, vk::PipelineStageFlags2 stage) {
	if (mCurrentMemoryStage == stage) {
		return;
	}

	deactivateRenderPass();
	UtilityVk::SetMemoryBarrier(cmdbuf, mCurrentMemoryStage, stage);

	mCurrentMemoryStage = stage;
}

void DriverVk::PushDescriptorBuffer(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
	const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding, vk::DescriptorType type, vk::Buffer buffer) {
	auto descriptor_buffer_info = vk::DescriptorBufferInfo().setBuffer(buffer).setRange(VK_WHOLE_SIZE);

	auto write_descriptor_set
		= vk::WriteDescriptorSet().setDstBinding(binding).setDescriptorCount(1).setDescriptorType(type).setBufferInfo(descriptor_buffer_info);

	cmdlist.pushDescriptorSetKHR(pipeline_bind_point, *pipeline_layout, 0, write_descriptor_set);
}

void DriverVk::PushDescriptorTexture(
	vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point, const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding) {
	// if (!gContext->sampler_states.contains(gContext->sampler_state)) {
	//	auto sampler = CreateSamplerState(gContext->sampler_state);
	//	gContext->sampler_states.insert({gContext->sampler_state, std::move(sampler)});
	// }

	auto texture = mTextures.at(binding);
	if (texture->mCurrentState != vk::ImageLayout::eGeneral) {
		// Image layout transitions (pipeline barriers) are not allowed inside a dynamic rendering scope.
		// The render pass is reactivated at the end of EnsureGraphicsState().
		deactivateRenderPass();
	}
	texture->setState(cmdlist, vk::ImageLayout::eGeneral);

	const auto& sampler = texture->mSampler;

	auto descriptor_image_info
		= vk::DescriptorImageInfo().setSampler(*sampler).setImageView(*(texture->mImageView)).setImageLayout(vk::ImageLayout::eGeneral);

	auto write_descriptor_set = vk::WriteDescriptorSet()
									.setDstBinding(binding)
									.setDescriptorCount(1)
									.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
									.setImageInfo(descriptor_image_info);

	cmdlist.pushDescriptorSetKHR(pipeline_bind_point, *pipeline_layout, 0, write_descriptor_set);
}

void DriverVk::PushDescriptorUniformBuffer(
	vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point, const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding) {
	auto buffer = mUniformBuffers.at(binding);

	PushDescriptorBuffer(cmdlist, pipeline_bind_point, pipeline_layout, binding, vk::DescriptorType::eUniformBuffer, *buffer->getBuffer());
}

void DriverVk::PushDescriptorStorageImage(
	vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point, const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding) {
	// auto texture = gContext->render_targets.at(0)->getTexture();
	// texture->ensureState(cmdlist, vk::ImageLayout::eGeneral);
	//
	// auto descriptor_image_info = vk::DescriptorImageInfo()
	//	.setImageLayout(vk::ImageLayout::eGeneral)
	//	.setImageView(*texture->getImageView());
	//
	// auto write_descriptor_set = vk::WriteDescriptorSet()
	//	.setDstBinding(binding)
	//	.setDescriptorCount(1)
	//	.setDescriptorType(vk::DescriptorType::eStorageImage)
	//	.setImageInfo(descriptor_image_info);
	//
	// cmdlist.pushDescriptorSetKHR(pipeline_bind_point,
	//	*pipeline_layout, 0, write_descriptor_set);
}

void DriverVk::PushDescriptorStorageBuffer(
	vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point, const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding) {
	auto buffer = mStorageBuffers.at(binding);

	PushDescriptorBuffer(cmdlist, pipeline_bind_point, pipeline_layout, binding, vk::DescriptorType::eStorageBuffer, *buffer->getBuffer());
}

void DriverVk::PushDescriptors(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
	const vk::raii::PipelineLayout& pipeline_layout, const std::vector<vk::DescriptorSetLayoutBinding>& required_descriptor_bindings,
	const std::unordered_set<uint32_t>& ignore_bindings = {}) {

	if (required_descriptor_bindings.empty() || !*mDescriptorPool || !mCurrentState.mShader) {
		return;
	}

	bool needsUpdate = false;
	for (const auto& required : required_descriptor_bindings) {
		if (required.descriptorCount > 1 || !ignore_bindings.contains(required.binding)) {
			needsUpdate = true;
			break;
		}
	}
	if (!needsUpdate) {
		return;
	}

	// A new set starts empty, so every binding is written, not only the dirty ones.
	// Image transitions are illegal inside a dynamic rendering pass.
	for (const auto& required : required_descriptor_bindings) {
		if (required.descriptorType != vk::DescriptorType::eCombinedImageSampler && required.descriptorType != vk::DescriptorType::eStorageImage) {
			continue;
		}
		if (required.descriptorCount > 1) {
			const auto& uniforms = mCurrentState.mShader->getReflection().mUniforms;
			const auto u = std::find_if(uniforms.begin(), uniforms.end(), [&](const auto& v) { return v.mBind == required.binding; });
			const auto& table = bindlessTable(*u, required.descriptorCount);
			for (uint32_t i = 0; i < required.descriptorCount; ++i) {
				auto texture = std::static_pointer_cast<TextureVk>(table.get(i).texture);
				if (!texture) {
					throw std::runtime_error("Bindless texture table contains buffer entries");
				}
				if (texture->mCurrentState != vk::ImageLayout::eGeneral) {
					deactivateRenderPass();
				}
				texture->setState(cmdlist, vk::ImageLayout::eGeneral);
			}
			continue;
		}
		auto texture = mTextures.at(required.binding);
		if (texture->mCurrentState != vk::ImageLayout::eGeneral) {
			deactivateRenderPass();
		}
		texture->setState(cmdlist, vk::ImageLayout::eGeneral);
	}

	struct BufferWrite {
		uint32_t binding = 0;
		uint32_t element = 0;
		vk::DescriptorType type = vk::DescriptorType::eUniformBuffer;
		vk::DescriptorBufferInfo info;
	};
	struct ImageWrite {
		uint32_t binding = 0;
		uint32_t element = 0;
		vk::DescriptorType type = vk::DescriptorType::eCombinedImageSampler;
		vk::DescriptorImageInfo info;
	};
	std::vector<BufferWrite> buffers;
	std::vector<ImageWrite> images;
	buffers.reserve(required_descriptor_bindings.size());
	images.reserve(required_descriptor_bindings.size());

	for (const auto& required : required_descriptor_bindings) {
		if (required.descriptorCount > 1) {
			const auto& uniforms = mCurrentState.mShader->getReflection().mUniforms;
			const auto u = std::find_if(uniforms.begin(), uniforms.end(), [&](const auto& v) { return v.mBind == required.binding; });
			const auto& table = bindlessTable(*u, required.descriptorCount);
			for (uint32_t i = 0; i < required.descriptorCount; ++i) {
				const auto entry = table.get(i);
				mBindlessFrameResources.push_back(entry);
				if (required.descriptorType == vk::DescriptorType::eCombinedImageSampler) {
					auto t = std::static_pointer_cast<TextureVk>(entry.texture);
					images.push_back({ required.binding, i, required.descriptorType,
						vk::DescriptorImageInfo(*t->mSampler, *t->mImageView, vk::ImageLayout::eGeneral) });
				}
				else {
					auto b = std::static_pointer_cast<StorageBufferVk>(entry.buffer);
					if (!b) {
						throw std::runtime_error("Bindless buffer table contains texture entries");
					}
					buffers.push_back(
						{ required.binding, i, required.descriptorType, vk::DescriptorBufferInfo(*b->getBuffer(), 0, b->getSizeByte()) });
				}
			}
			continue;
		}

		const uint32_t binding = required.binding;
		if (required.descriptorType == vk::DescriptorType::eCombinedImageSampler || required.descriptorType == vk::DescriptorType::eStorageImage) {
			auto texture = mTextures.at(binding);
			images.push_back(ImageWrite { binding, 0, required.descriptorType,
				vk::DescriptorImageInfo()
					.setSampler(*texture->mSampler)
					.setImageView(*texture->mImageView)
					.setImageLayout(vk::ImageLayout::eGeneral) });
		}
		else if (required.descriptorType == vk::DescriptorType::eUniformBuffer) {
			auto buffer = mUniformBuffers.at(binding);
			const vk::DeviceSize range = buffer->getSizeByte() > 0 ? buffer->getSizeByte() : VK_WHOLE_SIZE;
			buffers.push_back(BufferWrite { binding, 0, vk::DescriptorType::eUniformBuffer,
				vk::DescriptorBufferInfo().setBuffer(*buffer->getBuffer()).setOffset(0).setRange(range) });
		}
		else if (required.descriptorType == vk::DescriptorType::eStorageBuffer) {
			auto buffer = mStorageBuffers.at(binding);
			const vk::DeviceSize range = buffer->getSizeByte() > 0 ? buffer->getSizeByte() : VK_WHOLE_SIZE;
			buffers.push_back(BufferWrite { binding, 0, vk::DescriptorType::eStorageBuffer,
				vk::DescriptorBufferInfo().setBuffer(*buffer->getBuffer()).setOffset(0).setRange(range) });
		}
	}

	if (buffers.empty() && images.empty()) {
		return;
	}

	const vk::DescriptorSetLayout layout = *mCurrentState.mShader->mDescriptorSetLayout;
	auto allocInfo = vk::DescriptorSetAllocateInfo().setDescriptorPool(*mDescriptorPool).setDescriptorSetCount(1).setPSetLayouts(&layout);
	for (;;) {
		const auto pool = mDescriptorPoolIndex == 0 ? *mDescriptorPool : *mExtraDescriptorPools.at(mDescriptorPoolIndex - 1);
		allocInfo.setDescriptorPool(pool);
		try {
			auto allocated = mDevice.allocateDescriptorSets(allocInfo);
			mFrameDescriptorSets.push_back(std::move(allocated.front()));
			break;
		} catch (const vk::OutOfPoolMemoryError&) {
			// Growing the number of lights grows draws, not shadow textures.
		} catch (const vk::FragmentedPoolError&) { }
		++mDescriptorPoolIndex;
		if (mDescriptorPoolIndex > mExtraDescriptorPools.size()) {
			mExtraDescriptorPools.push_back(createDrawDescriptorPool());
			std::cout << "Vulkan draw descriptor pools=" << mExtraDescriptorPools.size() + 1 << std::endl;
		}
	}
	const vk::DescriptorSet set = *mFrameDescriptorSets.back();

	std::vector<vk::WriteDescriptorSet> writes;
	writes.reserve(buffers.size() + images.size());
	for (const auto& image : images) {
		writes.push_back(vk::WriteDescriptorSet()
				.setDstSet(set)
				.setDstBinding(image.binding)
				.setDstArrayElement(image.element)
				.setDescriptorCount(1)
				.setDescriptorType(image.type)
				.setImageInfo(image.info));
	}
	for (const auto& buffer : buffers) {
		writes.push_back(vk::WriteDescriptorSet()
				.setDstSet(set)
				.setDstBinding(buffer.binding)
				.setDstArrayElement(buffer.element)
				.setDescriptorCount(1)
				.setDescriptorType(buffer.type)
				.setBufferInfo(buffer.info));
	}
	mDevice.updateDescriptorSets(writes, {});
	cmdlist.bindDescriptorSets(pipeline_bind_point, *pipeline_layout, 0, set, {});
}

void DriverVk::resize(size_t width, size_t height) {
	auto found = mSwapchains.find(mCurrentWindowID);
	if (found == mSwapchains.end() || (!*found->second.swapchain && !found->second.sdkSwapchain)) {
		mWidth = static_cast<uint32_t>(width);
		mHeight = static_cast<uint32_t>(height);
		return;
	}
	auto& ctx = found->second;
	auto surface_capabilities = mPhysicalDevice.getSurfaceCapabilitiesKHR(*ctx.surface);
	uint32_t targetWidth = static_cast<uint32_t>(width);
	uint32_t targetHeight = static_cast<uint32_t>(height);
	if (surface_capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
		targetWidth = surface_capabilities.currentExtent.width;
		targetHeight = surface_capabilities.currentExtent.height;
	}
	if (targetWidth == ctx.width && targetHeight == ctx.height) {
		mWidth = targetWidth;
		mHeight = targetHeight;
		return;
	}
	if (mFrameGeneration) {
		mFrameGeneration->suspend();
	}
	end();
	wait();
	if (mFrameGeneration) {
		mFrameGeneration->waitForPresents();
	}
	mDevice.waitIdle();
	mWidth = targetWidth;
	mHeight = targetHeight;
	if (ctx.sdkResize) {
		auto resize = ctx.sdkResize;
		resize(mWidth, mHeight);
	}
	else {
		createSwapchain(mCurrentWindowID, mWidth, mHeight);
	}
	nextFrame();
	begin();
}

// void DriverVk::setVsync(bool value) {
//	// TODO: implement
// }

void DriverVk::setPrimitiveMode(PrimitiveMode topology) {
	setDirty(Dirty::PRIMITIVE_MODE);
	mPrimitiveMode = topology;
}

void DriverVk::setRasterization(RasterizationMode mode) {
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
	resetVertexStreams();
	mVertexBuffer = std::static_pointer_cast<VertexBufferVk>(buffer);
}

void DriverVk::setIndexBuffer(const std::shared_ptr<IndexBufferInterface> buffer) {
	setDirty(Dirty::INDEX_BUFFER);
	mIndexBuffer = std::static_pointer_cast<IndexBufferVk>(buffer);
	;
}

void DriverVk::setUniformBuffer(size_t binding, std::shared_ptr<UniformBufferInterface> handle) {
	mUniformBuffers[binding] = std::static_pointer_cast<UniformBufferVk>(handle);
	graphics_pipeline_ignore_bindings.erase(binding);
}

void DriverVk::setStorageBuffer(size_t binding, std::shared_ptr<StorageBufferInterface> handle) {
	mStorageBuffers[binding] = std::static_pointer_cast<StorageBufferVk>(handle);
	// graphics_pipeline_ignore_bindings.erase(binding);
}

void DriverVk::setTexture(const std::string& name, std::shared_ptr<TextureInterface> data) {
	if (!mCurrentState.mShader) {
		return;
	}
	const auto& ref = mCurrentState.mShader->getReflection();
	if (ref.mNameToUniforms.contains(name)) {
		size_t idx = ref.mNameToUniforms.at(name);
		setTexture(ref.mUniforms[idx].mBind, data);
	}
}

void DriverVk::setUniformBuffer(const std::string& name, std::shared_ptr<UniformBufferInterface> data) {
	if (!mCurrentState.mShader) {
		return;
	}
	const auto& ref = mCurrentState.mShader->getReflection();
	if (ref.mNameToUniforms.contains(name)) {
		size_t idx = ref.mNameToUniforms.at(name);
		setUniformBuffer(ref.mUniforms[idx].mBind, data);
	}
}

void DriverVk::setStorageBuffer(const std::string& name, std::shared_ptr<StorageBufferInterface> data) {
	if (!mCurrentState.mShader) {
		return;
	}
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

// void DriverVk::setTextureAddress(TextureAddress value) {
//	gContext->sampler_state.texture_address = value;
// }

void DriverVk::setTriangleOrientation(TriangleOrientation value) {
	setDirty(Dirty::TRIANGULATION_ORDER);
	mTriangleOrientation = value;
}

void DriverVk::clear(bool clearColor, bool clearDepth, bool clearStencil) {
	activateRenderPass();
	// vkCmdClearAttachments is clipped by the scissor. Set it before the clear,
	// otherwise MoltenVK keeps a stale rect and only part of the image is cleared.
	EnsureViewport(getCurrentFrame().mCommandBuffer);
	EnsureScissor(getCurrentFrame().mCommandBuffer);

	auto width = getBackbufferWidth();
	auto height = getBackbufferHeight();

	auto clear_rect = vk::ClearRect().setBaseArrayLayer(0).setLayerCount(1).setRect({ { 0, 0 }, { width, height } });

	if (clearColor) {

		auto clear_color_value = vk::ClearColorValue().setFloat32({ mClearColor.r, mClearColor.g, mClearColor.b, mClearColor.a });

		auto clear_value = vk::ClearValue().setColor(clear_color_value);

		const auto targets = mCurrentState.mFrameBuffer ? mCurrentState.mFrameBuffer : getCurrentFrame().mFrameBuffer;
		std::vector<vk::ClearAttachment> attachments;
		for (uint32_t i = 0; i < targets->getTextures().size(); ++i) {
			attachments.push_back(
				vk::ClearAttachment().setAspectMask(vk::ImageAspectFlagBits::eColor).setColorAttachment(i).setClearValue(clear_value));
		}
		if (!attachments.empty()) {
			getCurrentFrame().mCommandBuffer.clearAttachments(attachments, { clear_rect });
		}
	}

	if (clearDepth || clearStencil) {
		auto clear_depth_stencil_value = vk::ClearDepthStencilValue()
											 .setDepth(mDepthValue)
											 .setStencil((uint32_t)mStencilValue); // TODO: maybe we should change argument uint8_t -> uint32_t

		auto clear_value = vk::ClearValue().setDepthStencil(clear_depth_stencil_value);

		auto aspect_mask = vk::ImageAspectFlags();

		if (clearDepth) {
			aspect_mask |= vk::ImageAspectFlagBits::eDepth;
		}

		if (clearStencil) {
			aspect_mask |= vk::ImageAspectFlagBits::eStencil;
		}

		auto attachment = vk::ClearAttachment().setAspectMask(aspect_mask).setColorAttachment(0).setClearValue(clear_value);

		getCurrentFrame().mCommandBuffer.clearAttachments({ attachment }, { clear_rect });
	}
}

void DriverVk::draw(uint32_t vertex_count, uint32_t vertex_offset, uint32_t instance_count) {
	if (!vertex_count || !instance_count) {
		return;
	}
	validateVertexDraw({ vertex_count, vertex_offset, instance_count });
	EnsureGraphicsState(false);
	getCurrentFrame().mCommandBuffer.draw(vertex_count, instance_count, vertex_offset, 0);
}

std::vector<float> DriverVk::readTexture(std::shared_ptr<TextureInterface> texture, unsigned mip, unsigned layer) {
	const bool unorm = texture->getFormat() == PixelFormat::RGBA_INT || texture->getFormat() == PixelFormat::BGRA_INT;
	if (!unorm && texture->getFormat() != PixelFormat::RGBA_FLOAT && texture->getFormat() != PixelFormat::RG_FLOAT
		&& texture->getFormat() != PixelFormat::R_FLOAT && texture->getType() != TextureType::DEPTH) {
		throw std::runtime_error("Unsupported Vulkan readback format");
	}
	auto t = std::static_pointer_cast<TextureVk>(texture);
	const uint32_t w = std::max<size_t>(1, t->getWidth() >> mip), h = std::max<size_t>(1, t->getHeight() >> mip);
	const bool depth = t->getType() == TextureType::DEPTH;
	const bool red = t->getFormat() == PixelFormat::R_FLOAT && !depth;
	const unsigned channels = depth || red ? 1 : texture->getFormat() == PixelFormat::RG_FLOAT ? 2 : 4;
	std::vector<float> values(static_cast<size_t>(w) * h * channels);
	auto buffer = UtilityVk::CreateBuffer(
		values.size() * (unorm ? 1 : sizeof(float)), vk::BufferUsageFlagBits::eTransferDst, UtilityVk::MemoryUsage::Staging);
	const auto old = t->mCurrentState;
	UtilityVk::OneTimeSubmit([&](const auto& cmd) {
		t->setState(cmd, vk::ImageLayout::eTransferSrcOptimal);
		auto layers = vk::ImageSubresourceLayers()
						  .setAspectMask(depth ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor)
						  .setMipLevel(mip)
						  .setBaseArrayLayer(t->getType() == TextureType::TEXTURE_CUBE ? layer : 0)
						  .setLayerCount(1);
		auto copy = vk::BufferImageCopy()
						.setImageSubresource(layers)
						.setImageOffset({ 0, 0, t->getType() == TextureType::TEXTURE_3D ? static_cast<int>(layer) : 0 })
						.setImageExtent({ w, h, 1 });
		cmd.copyImageToBuffer(t->mImagePtr, vk::ImageLayout::eTransferSrcOptimal, *buffer, copy);
		t->setState(cmd, old);
	});
	void* mapped = nullptr;
	if (vmaMapMemory(mAllocator, buffer.allocation(), &mapped) != VK_SUCCESS) {
		throw std::runtime_error("Readback map failed");
	}
	vmaInvalidateAllocation(mAllocator, buffer.allocation(), 0, VK_WHOLE_SIZE);
	if (unorm) {
		const auto* bytes = static_cast<const uint8_t*>(mapped);
		for (size_t i = 0; i < values.size(); ++i) {
			values[i] = bytes[i] / 255.f;
		}
	}
	else {
		std::memcpy(values.data(), mapped, values.size() * sizeof(float));
	}
	vmaUnmapMemory(mAllocator, buffer.allocation());
	if (red) {
		std::vector<float> rgba(static_cast<size_t>(w) * h * 4, 0);
		for (size_t i = 0; i < values.size(); ++i) {
			rgba[i * 4] = values[i];
			rgba[i * 4 + 3] = 1;
		}
		return rgba;
	}
	return values;
}

void DriverVk::dispatch(uint32_t x, uint32_t y, uint32_t z) {
	auto& cmd = getCurrentFrame().mCommandBuffer;
	EnsureComputeState(cmd);
	cmd.dispatch(x, y, z);
	UtilityVk::SetMemoryBarrier(cmd, vk::PipelineStageFlagBits2::eComputeShader, vk::PipelineStageFlagBits2::eAllCommands);
}

void DriverVk::EnsureComputeState(vk::raii::CommandBuffer& cmd) {
	// Orders earlier graphics/transfer work (including indirect-argument reads) before compute writes.
	EnsureMemoryState(cmd, vk::PipelineStageFlagBits2::eComputeShader);
	deactivateRenderPass();
	auto shader = mCurrentState.mShader;
	if (!*shader->mComputeShaderModule) {
		throw std::runtime_error("Dispatch requires compute shader");
	}
	const std::string key = "compute:" + std::to_string(shader->getId());
	if (!mStates.contains(key)) {
		auto stage = vk::PipelineShaderStageCreateInfo()
						 .setStage(vk::ShaderStageFlagBits::eCompute)
						 .setModule(*shader->mComputeShaderModule)
						 .setPName("main");
		auto info = vk::ComputePipelineCreateInfo().setStage(stage).setLayout(*shader->mPipelineLayout);
		UTILS::LoadTimer timing("compute-pipeline", shader->mPath);
		mStates.emplace(key, mDevice.createComputePipeline(nullptr, info));
#if IKIGAI_ENABLE_MEMORY_TRACKING
		const auto handle = PROFILER::GpuHandleValue(static_cast<VkPipeline>(*mStates.at(key)));
		try {
			mGpuPipelines.emplace(handle,
				PROFILER::TrackGpuObject(PROFILER::GpuBackend::Vulkan, reinterpret_cast<std::uintptr_t>(static_cast<VkDevice>(*mDevice)), handle,
					PROFILER::GpuResourceKind::Pipeline, "Compute pipeline"));
		} catch (...) {
			PROFILER::GpuMemoryProfiler::instance().noteDroppedRecord();
		}
#endif
	}
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *mStates.at(key));
	std::unordered_set<uint32_t> ignored;
	PushDescriptors(cmd, vk::PipelineBindPoint::eCompute, shader->mPipelineLayout, shader->mRequiredDescriptorBindings, ignored);
	mBoundPipelineName.clear();
	graphics_pipeline_ignore_bindings.clear();
	setDirty(Dirty::PIPELINE);
}

void DriverVk::drawIndexed(uint32_t index_count, uint32_t index_offset, uint32_t instance_count) {
	if (!index_count || !instance_count) {
		return;
	}
	validateVertexDraw({ index_count, index_offset, instance_count, 0, 0, true });
	EnsureGraphicsState(true);
	getCurrentFrame().mCommandBuffer.drawIndexed(index_count, instance_count, index_offset, 0, 0);
}

void DriverVk::setClearColor(const MATH::Vector4f& color) {
	mClearColor = color;
}

void DriverVk::setClearColor(float r, float g, float b, float a) {
	mClearColor = { r, g, b, a };
}

void DriverVk::submit() {
	if (!mPresentationUIRendered) {
		if (auto* imgui = IKIGAI::IMGUI::Get()) {
			imgui->renderDrawData();
		}
	}
	end();
	if (IKIGAI::CAPTURE::ScreenshotService::Get().pending()) {
		const bool bgra = mSurfaceFormat.format == vk::Format::eB8G8R8A8Unorm || mSurfaceFormat.format == vk::Format::eB8G8R8A8Srgb;
		IKIGAI::CAPTURE::ScreenshotService::Get().captureTexture(*this, getCurrentFrame().mFrameBuffer->getTextures().at(0), bgra);
	}

	const auto& render_complete_semaphore = getCurrentFrame().mRenderCompleteSemaphore;
	latencySubmitEnd();

	vk::PresentInfoKHR presentInfo {};
	presentInfo.setWaitSemaphores(*render_complete_semaphore);
	auto& ctx = getCurrentSwapchainContext();
	const vk::SwapchainKHR presentedSwapchain = ctx.sdkSwapchain ? vk::SwapchainKHR(ctx.sdkSwapchain) : *ctx.swapchain;
	presentInfo.setSwapchains(presentedSwapchain);
	presentInfo.setImageIndices(mFrameIndex);
#ifdef IKIGAI_USE_STREAMLINE
	if (mDlssRuntime && !ctx.streamline) {
		std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->commonPresentBefore();
	}
#endif

	if (ctx.sdkPresent) {
#ifdef IKIGAI_USE_STREAMLINE
		if (mDlssRuntime) {
			std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->marker(sl::PCLMarker::ePresentStart);
		}
#endif
		const auto result = ctx.sdkPresent(reinterpret_cast<const VkPresentInfoKHR*>(&presentInfo));
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			throw std::runtime_error("SDK present failed: " + std::to_string(result));
		}
	}
	else {
		auto present_result = mQueue.presentKHR(presentInfo);
		(void)present_result;
	}
#ifdef IKIGAI_USE_STREAMLINE
	if (mDlssRuntime && !ctx.streamline) {
		std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->commonPresentAfter();
	}
	if (mDlssRuntime) {
		std::static_pointer_cast<StreamlineRuntime>(mDlssRuntime)->marker(sl::PCLMarker::ePresentEnd);
	}
#endif

	mSemaphoreIndex = (mSemaphoreIndex + 1) % getCurrentSwapchainContext().frames.size();

	wait();
	if (mFrameGeneration) {
		mFrameGeneration->waitForPresents();
		mFrameGeneration->afterPresent();
		mFrameGenerationStatus = mFrameGeneration->status();
	}
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

std::shared_ptr<TextureInterface> DriverVk::createTexture(
	const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureVk::Create(path, allocator, deleter);
}
std::shared_ptr<TextureInterface> DriverVk::createTextureAtlas(
	const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureAtlasVk::CreateAtlas(path, generateMipmap, allocator, deleter);
}
std::shared_ptr<TextureInterface> DriverVk::createTexture(const TextureResource& res, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureVk::Create(res, allocator, deleter);
}
std::shared_ptr<TextureInterface> DriverVk::createTexture(
	const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureVk::Create(res, fileData, allocator, deleter);
}
std::shared_ptr<TextureInterface> DriverVk::createTextureAtlas(
	const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	return TextureAtlasVk::CreateAtlasFromResource(res, fileData, allocator, deleter);
}
std::shared_ptr<TextureInterface> DriverVk::createTexture(
	const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	// TextureLoader::CreateFromFile() goes through this overload. It used to return nullptr, so every
	// file texture silently fell back to the default (pink) texture.
	TextureResource res;
	res.path = name;
	res.useMipmap = generateMipmap;
	res.pathTexture.push_back(name);
	return TextureVk::Create(res, std::vector<std::vector<uint8_t>> { data }, allocator, deleter);
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
		if (deleter) {
			deleter(m);
		}
	};
	return AllocateModel<ModelVk>(allocator, std::move(finalDeleter), path);
}

std::shared_ptr<MaterialInterface> DriverVk::createMaterial(const MaterialResource& res, UTILS::IAllocator* allocator, MaterialDeleter deleter) {
	MaterialDeleter finalDeleter = [deleter](MaterialInterface* m) {
		if (deleter) {
			deleter(m);
		}
	};
	return AllocateMaterial<MaterialVk>(allocator, std::move(finalDeleter), res);
}

std::shared_ptr<FrameBufferInterface> DriverVk::createFrameBuffer(
	const std::vector<std::shared_ptr<TextureInterface>>& textures, std::shared_ptr<TextureInterface> depth, unsigned mip, unsigned layer) {
	return std::make_shared<FrameBufferVk>(textures, depth, mip, layer);
}

void DriverVk::draw(const MeshInterface& mesh, PrimitiveMode primitive, uint32_t instances) {
	if (instances > 0) {
		setPrimitiveMode(primitive);
		mesh.bind();
		if (mesh.getIndexCount() > 0) {
			drawIndexed(static_cast<uint32_t>(mesh.getIndexCount()), 0, instances);
		}
		else {
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
	deactivateRenderPass();
	UtilityVk::SetMemoryBarrier(getCurrentFrame().mCommandBuffer, vk::PipelineStageFlagBits2::eAllGraphics, vk::PipelineStageFlagBits2::eAllGraphics);
	mCurrentState.mFrameBuffer = std::static_pointer_cast<FrameBufferVk>(frameBuffer);
}

void DriverVk::resetFrameBuffer() {
	setFrameBuffer(nullptr);
}

void IKIGAI::RENDER::DriverVk::draw(const DrawArguments& args) {
	if (!args.count || !args.instanceCount) {
		return;
	}
	validateVertexDraw(args);
	EnsureGraphicsState(args.indexed);
	auto& cmd = getCurrentFrame().mCommandBuffer;
	if (args.indexed) {
		cmd.drawIndexed(args.count, args.instanceCount, args.first, args.baseVertex, args.firstInstance);
	}
	else {
		cmd.draw(args.count, args.instanceCount, args.first, args.firstInstance);
	}
}

IKIGAI::RENDER::BindlessCapabilities IKIGAI::RENDER::DriverVk::bindlessCapabilities() const {
	return mBindlessCapabilities;
}
uint32_t IKIGAI::RENDER::DriverVk::maxFragmentSampledTextures() const {
	const auto limits = mPhysicalDevice.getProperties().limits;
	return std::min(limits.maxPerStageDescriptorSampledImages, limits.maxPerStageDescriptorSamplers);
}
#endif
