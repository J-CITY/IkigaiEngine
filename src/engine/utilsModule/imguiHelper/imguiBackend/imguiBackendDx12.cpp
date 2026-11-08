#ifdef DX12_BACKEND
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_sdl3.h"
#include "imgui.h"
#include "imguiBackend.h"
#include "renderModule/backends/dx12/d3dUtil.h"
#include "renderModule/backends/dx12/driverDx12.h"
#include "renderModule/backends/dx12/frameBufferDx12.h"
#include "renderModule/backends/dx12/textureDx12.h"
#include "renderModule/backends/interface/textureInterface.h"
#include "windowModule/window/window.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace IKIGAI::IMGUI {
void ConfigureImGuiContext();
}

namespace {
class ImGuiBackendDx12 final : public IKIGAI::IMGUI::IImGuiBackend {
	int mPlatformFrame = -1;

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
		info.NumFramesInFlight
			= std::max<unsigned>(IKIGAI::RENDER::DriverDx12::DEFAULT_FB_SIZE, IKIGAI::RENDER::DriverInterface::settings.framesInFlight);
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
		renderDrawDataTo({});
	}
	void renderDrawDataTo(std::shared_ptr<IKIGAI::RENDER::FrameBufferInterface> target) override {
		auto* driverDx12 = IKIGAI::RENDER::d3dUtil::GetDriver();
		if (!driverDx12 || !ImGui::GetDrawData()) {
			return;
		}
		ImGuiIO& io = ImGui::GetIO();
		// The last graph pass may target a float texture. Engine framebuffer
		// state is lazy, while ImGui records commands directly into this list.
		auto frame = target ? std::static_pointer_cast<IKIGAI::RENDER::FrameBufferDx12>(target) : driverDx12->getDefaultFrameBuffer();
		auto command = driverDx12->getCommandList().Get();
		for (const auto& texture : frame->getTextures()) {
			std::static_pointer_cast<IKIGAI::RENDER::TextureDx12>(texture)->setState(command, D3D12_RESOURCE_STATE_RENDER_TARGET);
		}
		if (frame->getDepth()) {
			std::static_pointer_cast<IKIGAI::RENDER::TextureDx12>(frame->getDepth())->setState(command, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		}
		const auto color = frame->getTexHeap()->GetCPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE depth {};
		if (frame->getDepth()) {
			depth = frame->getDepthHeap()->GetCPUDescriptorHandleForHeapStart();
		}
		command->OMSetRenderTargets(1, &color, FALSE, frame->getDepth() ? &depth : nullptr);
		auto* drawData = ImGui::GetDrawData();
		// Node editor draw commands may have an unbounded clip rect. Clamp
		// before the DX12 backend converts floats to signed LONG scissors.
		const auto origin = drawData->DisplayPos;
		const ImVec2 limit(origin.x + drawData->DisplaySize.x, origin.y + drawData->DisplaySize.y);
		for (int i = 0; i < drawData->CmdListsCount; ++i) {
			for (auto& draw : drawData->CmdLists[i]->CmdBuffer) {
				draw.ClipRect.x = std::clamp(draw.ClipRect.x, origin.x, limit.x);
				draw.ClipRect.y = std::clamp(draw.ClipRect.y, origin.y, limit.y);
				draw.ClipRect.z = std::clamp(draw.ClipRect.z, origin.x, limit.x);
				draw.ClipRect.w = std::clamp(draw.ClipRect.w, origin.y, limit.y);
			}
		}
		ImGui_ImplDX12_RenderDrawData(drawData, driverDx12->getCommandList().Get());
		// Initialization and offscreen work may submit the same UI draw data more than once.
		const int imguiFrame = ImGui::GetFrameCount();
		if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) && mPlatformFrame != imguiFrame) {
			mPlatformFrame = imguiFrame;
			ImGui::UpdatePlatformWindows();
			// Platform windows execute their own command lists on the queue right away and may sample textures whose
			// transitions to a shader-resource state were recorded in this frame's list. Run that list first.
			if (ImGui::GetPlatformIO().Viewports.Size > 1) {
				driverDx12->executeRecordedCommands();
			}
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
