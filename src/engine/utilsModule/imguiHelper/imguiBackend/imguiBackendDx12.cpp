#ifdef DX12_BACKEND
#include "imguiBackend.h"
#include <SDL.h>
#include "windowModule/window/window.h"
#include "renderModule/backends/dx12/driverDx12.h"
#include "renderModule/backends/dx12/d3dUtil.h"
#include "renderModule/backends/interface/textureInterface.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_dx12.h"
#include "imgui.h"

namespace IKIGAI::IMGUI {
	void ConfigureImGuiContext();
}

namespace {
	class ImGuiBackendDx12 final : public IKIGAI::IMGUI::IImGuiBackend {
	public:
		bool init(IKIGAI::WINDOW::Window& window, IKIGAI::RENDER::DriverInterface& driver) override {
			IKIGAI::IMGUI::ConfigureImGuiContext();
			if (!ImGui_ImplSDL2_InitForD3D(window.getSDLWindow())) {
				return false;
			}

			auto* driverDx12 = static_cast<IKIGAI::RENDER::DriverDx12*>(&driver);
			const bool ok = ImGui_ImplDX12_Init(
				driverDx12->getDevice().Get(),
				IKIGAI::RENDER::DriverDx12::DEFAULT_FB_SIZE,
				DXGI_FORMAT_R8G8B8A8_UNORM,
				driverDx12->getDescriptorHeap().Get(),
				driverDx12->getDescriptorHeapCPUHandle(),
				driverDx12->getDescriptorHeapGPUHandle());
			driverDx12->getDescriptorHeapCPUHandle().Offset(1, driverDx12->getDescriptorIncSize());
			driverDx12->getDescriptorHeapGPUHandle().Offset(1, driverDx12->getDescriptorIncSize());
			return ok;
		}

		void shutdown() override {
			ImGui_ImplDX12_Shutdown();
			ImGui_ImplSDL2_Shutdown();
			ImGui::DestroyContext();
		}

		void newFrame() override {
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplSDL2_NewFrame();
			ImGui::NewFrame();
		}

		void endFrame() override {
			ImGui::Render();
		}

		void renderDrawData() override {
			auto* driverDx12 = IKIGAI::RENDER::d3dUtil::GetDriver();
			if (!driverDx12 || !ImGui::GetDrawData()) {
				return;
			}
			ImGuiIO& io = ImGui::GetIO();
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), driverDx12->getCommandList().Get());
			if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
				ImGui::UpdatePlatformWindows();
				ImGui::RenderPlatformWindowsDefault();
			}
		}

		void* registerGpuTexture(IKIGAI::RENDER::TextureInterface& texture) override {
			return texture.getImguiId();
		}

		void invalidateDeviceObjects() override {
			ImGui_ImplDX12_InvalidateDeviceObjects();
		}

		void processEvent(const void* sdlEvent) override {
			ImGui_ImplSDL2_ProcessEvent(static_cast<const SDL_Event*>(sdlEvent));
		}
	};
}

std::unique_ptr<IKIGAI::IMGUI::IImGuiBackend> CreateImGuiBackendDx12() {
	return std::make_unique<ImGuiBackendDx12>();
}
#endif
