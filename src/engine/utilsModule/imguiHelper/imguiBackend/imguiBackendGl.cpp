#ifdef OPENGL_BACKEND
#include "imguiBackend.h"
#include <SDL3/SDL.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"
#include "imgui.h"
#include "renderModule/backends/interface/textureInterface.h"
#include "windowModule/window/window.h"
#include <algorithm>
#include <cmath>

namespace IKIGAI::IMGUI {
void ConfigureImGuiContext();
}

namespace {
void ClampDrawClipRects(ImDrawData* drawData) {
	if (!drawData) {
		return;
	}
	const ImVec2 minimum = drawData->DisplayPos;
	const ImVec2 maximum(minimum.x + drawData->DisplaySize.x, minimum.y + drawData->DisplaySize.y);
	if (!std::isfinite(minimum.x) || !std::isfinite(minimum.y) || !std::isfinite(maximum.x) || !std::isfinite(maximum.y) || maximum.x < minimum.x
		|| maximum.y < minimum.y) {
		return;
	}
	// ImGui allows clip rectangles larger than the display (including fullscreen sentinels).
	// Intersect in logical coordinates before the GL backend scales and converts them to GLsizei.
	for (auto* list : drawData->CmdLists) {
		for (auto& command : list->CmdBuffer) {
			if (command.UserCallback) {
				continue;
			}
			auto& clip = command.ClipRect;
			if (std::isnan(clip.x) || std::isnan(clip.y) || std::isnan(clip.z) || std::isnan(clip.w)) {
				clip = ImVec4(minimum.x, minimum.y, minimum.x, minimum.y);
				continue;
			}
			clip.x = std::clamp(clip.x, minimum.x, maximum.x);
			clip.y = std::clamp(clip.y, minimum.y, maximum.y);
			clip.z = std::clamp(clip.z, minimum.x, maximum.x);
			clip.w = std::clamp(clip.w, minimum.y, maximum.y);
		}
	}
}

class ImGuiBackendOpenGL final : public IKIGAI::IMGUI::IImGuiBackend {
public:
	bool init(IKIGAI::WINDOW::Window& window, IKIGAI::RENDER::DriverInterface&) override {
		IKIGAI::IMGUI::ConfigureImGuiContext();
		if (!ImGui_ImplSDL3_InitForOpenGL(window.getSDLWindow(), window.getGLContext())) {
			return false;
		}
#ifdef __EMSCRIPTEN__
		const char* glslVersion = "#version 300 es";
#elif defined(__APPLE__)
#if TARGET_OS_IPHONE
		const char* glslVersion = "#version 300 es";
#else
		const char* glslVersion = "#version 150";
#endif
#elif defined(__ANDROID__)
#if !defined(IKIGAI_GLES_VERSION) || IKIGAI_GLES_VERSION >= 300
		const char* glslVersion = "#version 300 es";
#else
		const char* glslVersion = "#version 100";
#endif
#else
		const char* glslVersion = "#version 330";
#endif
		return ImGui_ImplOpenGL3_Init(glslVersion);
	}

	void shutdown() override {
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
	}

	void newFrame() override {
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();
	}

	void endFrame() override {
		ImGui::Render();
	}

	void renderDrawData() override {
		ImGuiIO& io = ImGui::GetIO();
		ClampDrawClipRects(ImGui::GetDrawData());
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			SDL_Window* backupWindow = SDL_GL_GetCurrentWindow();
			SDL_GLContext backupContext = SDL_GL_GetCurrentContext();
			ImGui::UpdatePlatformWindows();
			for (auto* viewport : ImGui::GetPlatformIO().Viewports) {
				ClampDrawClipRects(viewport->DrawData);
			}
			ImGui::RenderPlatformWindowsDefault();
			SDL_GL_MakeCurrent(backupWindow, backupContext);
		}
	}

	void* registerGpuTexture(IKIGAI::RENDER::TextureInterface& texture) override {
		return texture.getImguiId();
	}

	void invalidateDeviceObjects() override {
		ImGui_ImplOpenGL3_DestroyDeviceObjects();
	}

	void processEvent(const void* sdlEvent) override {
		ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(sdlEvent));
	}
};
}

std::unique_ptr<IKIGAI::IMGUI::IImGuiBackend> CreateImGuiBackendOpenGL() {
	return std::make_unique<ImGuiBackendOpenGL>();
}
#endif
