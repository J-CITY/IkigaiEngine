#ifdef VULKAN_BACKEND
#include "imguiBackend.h"
#include <SDL3/SDL.h>
#include <cstring>
#include "windowModule/window/window.h"
#include "renderModule/backends/vk/driverVk.h"
#include "renderModule/backends/vk/textureVk.h"
#include "renderModule/backends/vk/helpers.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"

namespace IKIGAI::IMGUI {
	void ConfigureImGuiContext();
}

namespace {
	class ImGuiBackendVulkan final : public IKIGAI::IMGUI::IImGuiBackend {
	public:
		bool init(IKIGAI::WINDOW::Window& window, IKIGAI::RENDER::DriverInterface& driver) override {
			IKIGAI::IMGUI::ConfigureImGuiContext();
			if (!ImGui_ImplSDL3_InitForVulkan(window.getSDLWindow())) {
				return false;
			}

			auto* driverVk = static_cast<IKIGAI::RENDER::DriverVk*>(&driver);

			VkDescriptorPoolSize poolSizes[] = {
				{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
				{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
				{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
				{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
				{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
				{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
				{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
				{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
				{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
				{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
				{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
			};
			VkDescriptorPoolCreateInfo poolInfo = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
				.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
				.maxSets = 1000,
				.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes)),
				.pPoolSizes = poolSizes
			};
			vkCreateDescriptorPool(*driverVk->mDevice, &poolInfo, nullptr, &driverVk->mImguiPool);

			ImGui_ImplVulkan_InitInfo initInfo = {};
			initInfo.Instance = *driverVk->mInstance;
			initInfo.PhysicalDevice = *driverVk->mPhysicalDevice;
			initInfo.Device = *driverVk->mDevice;
			initInfo.QueueFamily = driverVk->mQueueFamilyIndex;
			initInfo.Queue = *driverVk->mQueue;
			initInfo.DescriptorPool = driverVk->mImguiPool;
			initInfo.PipelineInfoMain.Subpass = 0;
			initInfo.MinImageCount = 3;
			initInfo.ImageCount = 3;
			initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
			initInfo.UseDynamicRendering = true;

			static auto swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;
			initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
			initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
			initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainImageFormat;
			initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
			initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;

			ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, [](const char* functionName, void* vulkanInstance) {
				if (strcmp("vkCmdBeginRenderingKHR", functionName) == 0) {
					return vkGetInstanceProcAddr(*(reinterpret_cast<VkInstance*>(vulkanInstance)), "vkCmdBeginRendering");
				}
				if (strcmp("vkCmdEndRenderingKHR", functionName) == 0) {
					return vkGetInstanceProcAddr(*(reinterpret_cast<VkInstance*>(vulkanInstance)), "vkCmdEndRendering");
				}
				return vkGetInstanceProcAddr(*(reinterpret_cast<VkInstance*>(vulkanInstance)), functionName);
			}, &initInfo.Instance);

			return ImGui_ImplVulkan_Init(&initInfo);
		}

		void shutdown() override {
			auto* driverVk = IKIGAI::RENDER::UtilityVk::GetDriver();
			ImGui_ImplVulkan_Shutdown();
			if (driverVk && driverVk->mImguiPool) {
				vkDestroyDescriptorPool(*driverVk->mDevice, driverVk->mImguiPool, nullptr);
				driverVk->mImguiPool = VK_NULL_HANDLE;
			}
			ImGui_ImplSDL3_Shutdown();
			ImGui::DestroyContext();
		}

		void newFrame() override {
			ImGui_ImplVulkan_NewFrame();
			ImGui_ImplSDL3_NewFrame();
			ImGui::NewFrame();
		}

		void endFrame() override {
			ImGui::Render();
		}

		void renderDrawData() override {
			auto* driverVk = IKIGAI::RENDER::UtilityVk::GetDriver();
			if (!driverVk || !ImGui::GetDrawData()) {
				return;
			}
			ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *driverVk->getCurrentFrame().mCommandBuffer);
			ImGuiIO& io = ImGui::GetIO();
			if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
				ImGui::UpdatePlatformWindows();
				ImGui::RenderPlatformWindowsDefault();
			}
		}

		void* registerGpuTexture(IKIGAI::RENDER::TextureInterface& texture) override {
			auto& textureVk = static_cast<IKIGAI::RENDER::TextureVk&>(texture);
			if (!textureVk.mDescriptorSet) {
				VkSamplerCreateInfo samplerCreateInfo = {};
				samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
				samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
				samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
				static auto sampler = IKIGAI::RENDER::UtilityVk::GetDriver()->mDevice.createSampler(samplerCreateInfo);
				textureVk.mDescriptorSet = ImGui_ImplVulkan_AddTexture(*sampler, *textureVk.mImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			}
			return reinterpret_cast<void*>(textureVk.mDescriptorSet);
		}

		void invalidateDeviceObjects() override {}

		void processEvent(const void* sdlEvent) override {
			ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(sdlEvent));
		}
	};
}

std::unique_ptr<IKIGAI::IMGUI::IImGuiBackend> CreateImGuiBackendVulkan() {
	return std::make_unique<ImGuiBackendVulkan>();
}
#endif
