#ifdef DX12_BACKEND
#include "imguiBackend.h"
#include <SDL3/SDL.h>
#include "windowModule/window/window.h"
#include "renderModule/backends/dx12/driverDx12.h"
#include "renderModule/backends/dx12/d3dUtil.h"
#include "renderModule/backends/interface/textureInterface.h"
#include "backends/imgui_impl_sdl3.h"
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
			if (!ImGui_ImplSDL3_InitForD3D(window.getSDLWindow())) {
				return false;
			}

			auto* driverDx12 = static_cast<IKIGAI::RENDER::DriverDx12*>(&driver);
			ImGui_ImplDX12_InitInfo info;
			info.Device = driverDx12->getDevice().Get();
			info.CommandQueue = driverDx12->getCommandQueue().Get();
			info.NumFramesInFlight = IKIGAI::RENDER::DriverDx12::DEFAULT_FB_SIZE;
			info.RTVFormat = IKIGAI::RENDER::DriverDx12::DefaultTextureColorFormat;
			info.DSVFormat = IKIGAI::RENDER::DriverDx12::DefaultDepthFormat;
			info.SrvDescriptorHeap = driverDx12->getDescriptorHeap().Get();
			info.UserData = driverDx12;
			info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* initInfo, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
				static_cast<IKIGAI::RENDER::DriverDx12*>(initInfo->UserData)->allocImGuiSrv(outCpu, outGpu);
			};
			info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* initInfo, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
				static_cast<IKIGAI::RENDER::DriverDx12*>(initInfo->UserData)->freeImGuiSrv(cpu, gpu);
			};
			return ImGui_ImplDX12_Init(&info);
		}

		void shutdown() override {
			ImGui_ImplDX12_Shutdown();
			ImGui_ImplSDL3_Shutdown();
			ImGui::DestroyContext();
		}

		void newFrame() override {
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplSDL3_NewFrame();
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
			ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(sdlEvent));
		}
	};
}

std::unique_ptr<IKIGAI::IMGUI::IImGuiBackend> CreateImGuiBackendDx12() {
	return std::make_unique<ImGuiBackendDx12>();
}
#endif
