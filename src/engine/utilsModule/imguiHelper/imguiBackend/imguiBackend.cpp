#include "imguiBackend.h"

#include "imgui.h"

#ifdef OPENGL_BACKEND
std::unique_ptr<IKIGAI::IMGUI::IImGuiBackend> CreateImGuiBackendOpenGL();
#endif
#ifdef VULKAN_BACKEND
std::unique_ptr<IKIGAI::IMGUI::IImGuiBackend> CreateImGuiBackendVulkan();
#endif
#ifdef DX12_BACKEND
std::unique_ptr<IKIGAI::IMGUI::IImGuiBackend> CreateImGuiBackendDx12();
#endif
#ifdef METAL_BACKEND
std::unique_ptr<IKIGAI::IMGUI::IImGuiBackend> CreateImGuiBackendMetal();
#endif

namespace IKIGAI::IMGUI {
	namespace {
		IImGuiBackend* gActive = nullptr;
	}

	void ConfigureImGuiContext() {
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
		if (RENDER::DriverInterface::settings.backend == RENDER::RenderSettings::Backend::OPENGL) {
			io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
		}
#endif

		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}
	}

	std::unique_ptr<IImGuiBackend> CreateImGuiBackend(RENDER::RenderSettings::Backend backend) {
		std::unique_ptr<IImGuiBackend> backendImpl;
		switch (backend) {
#ifdef OPENGL_BACKEND
		case RENDER::RenderSettings::Backend::OPENGL:
			backendImpl = CreateImGuiBackendOpenGL();
			break;
#endif
#ifdef VULKAN_BACKEND
		case RENDER::RenderSettings::Backend::VULKAN:
			backendImpl = CreateImGuiBackendVulkan();
			break;
#endif
#ifdef DX12_BACKEND
		case RENDER::RenderSettings::Backend::DIRECTX12:
			backendImpl = CreateImGuiBackendDx12();
			break;
#endif
#ifdef METAL_BACKEND
		case RENDER::RenderSettings::Backend::METAL:
			backendImpl = CreateImGuiBackendMetal();
			break;
#endif
		default:
			break;
		}
		return backendImpl;
	}

	IImGuiBackend* Get() {
		return gActive;
	}

	void SetActive(IImGuiBackend* backend) {
		gActive = backend;
	}

	void ShutdownActive() {
		if (gActive) {
			gActive->shutdown();
			gActive = nullptr;
		}
	}
}
