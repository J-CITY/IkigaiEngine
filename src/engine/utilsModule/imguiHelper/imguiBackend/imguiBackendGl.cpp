#ifdef OPENGL_BACKEND
#include "imguiBackend.h"
#include <SDL3/SDL.h>
#include "windowModule/window/window.h"
#include "renderModule/backends/interface/textureInterface.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"

namespace IKIGAI::IMGUI {
	void ConfigureImGuiContext();
}

namespace {
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
			const char* glslVersion = "#version 150";
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
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
				SDL_Window* backupWindow = SDL_GL_GetCurrentWindow();
				SDL_GLContext backupContext = SDL_GL_GetCurrentContext();
				ImGui::UpdatePlatformWindows();
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
