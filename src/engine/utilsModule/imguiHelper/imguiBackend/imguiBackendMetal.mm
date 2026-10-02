#ifdef METAL_BACKEND
#include "imguiBackend.h"
#include "windowModule/window/window.h"
#include "renderModule/backends/metal/driverMetal.h"
#include "renderModule/backends/interface/textureInterface.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_metal.h"
#include "imgui.h"

namespace IKIGAI::IMGUI {
	void ConfigureImGuiContext();
}

namespace {
	class ImGuiBackendMetal final : public IKIGAI::IMGUI::IImGuiBackend {
	public:
		bool init(IKIGAI::WINDOW::Window& window, IKIGAI::RENDER::DriverInterface& driver) override {
			IKIGAI::IMGUI::ConfigureImGuiContext();
			if (!ImGui_ImplSDL3_InitForMetal(window.getSDLWindow())) {
				return false;
			}
			auto* driverMetal = static_cast<IKIGAI::RENDER::DriverMetal*>(&driver);
			mDevice = driverMetal->getDevice();
			mPassDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
			mPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionLoad;
			mPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
			return ImGui_ImplMetal_Init(mDevice);
		}

		void shutdown() override {
			ImGui_ImplMetal_Shutdown();
			ImGui_ImplSDL3_Shutdown();
			ImGui::DestroyContext();
			mPassDescriptor = nil;
			mDevice = nil;
		}

		void newFrame() override {
			ImGui_ImplMetal_NewFrame(mPassDescriptor);
			ImGui_ImplSDL3_NewFrame();
			ImGui::NewFrame();
		}

		void endFrame() override {
			ImGui::Render();
		}

		void renderDrawData() override {
			auto* driverMetal = static_cast<IKIGAI::RENDER::DriverMetal*>(IKIGAI::RENDER::DriverInterface::Get());
			if (!driverMetal || !ImGui::GetDrawData()) {
				return;
			}
			ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), driverMetal->getCurrentCommandBuffer(), driverMetal->getCurrentEncoder());
			ImGuiIO& io = ImGui::GetIO();
			if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
				ImGui::UpdatePlatformWindows();
				ImGui::RenderPlatformWindowsDefault();
			}
		}

		void* registerGpuTexture(IKIGAI::RENDER::TextureInterface& texture) override {
			return texture.getImguiId();
		}

		void invalidateDeviceObjects() override {
			ImGui_ImplMetal_DestroyDeviceObjects();
		}

		void processEvent(const void* sdlEvent) override {
			ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(sdlEvent));
		}

	private:
		id<MTLDevice> mDevice = nil;
		MTLRenderPassDescriptor* mPassDescriptor = nil;
	};
}

std::unique_ptr<IKIGAI::IMGUI::IImGuiBackend> CreateImGuiBackendMetal() {
	return std::make_unique<ImGuiBackendMetal>();
}
#endif
