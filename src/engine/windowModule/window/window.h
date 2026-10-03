#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <utilsModule/event.h>
#include <mathModule/math.h>

#include "windowModule/inputManager/gamepad/gamepad.h"

//need for right including dependencies
//#include <GLFW/glfw3.h>
#ifdef OCULUS

#include "util_egl.h"
#include "util_oxr.h"
#include <android/input.h>
#endif

#ifdef USE_GLFW
#include <coreModule/graphicsWrapper.hpp>
#include "GLFW/glfw3.h"
#endif

#include "renderModule/backends/interface/driverInterface.h"

#ifdef DX12_BACKEND
#include <d3d12.h>
#endif

#ifdef VULKAN_BACKEND
#include <volk.h>
#endif

struct SDL_Window;

namespace IKIGAI::WINDOW {
	struct WindowSettings {
		bool isFullscreen = false;
		bool isCursorVisible = true;
		bool isCursorLock = true;
		int depthBits = 24;
		int stencilBits = 8;
		int antialiasingLevel = 4;
		int majorVersion = 4;
		int minorVersion = 3;
		std::string title;
		MATH::Vector2u size = MATH::Vector2u(800, 600);
		int refreshRate = 60;
		RENDER::RenderSettings::Backend renderBackend = RENDER::RenderSettings::Backend::OPENGL;

		//template<class Context>
		//constexpr static auto serde(Context& context, WindowSettings& value) {
		//	using Self = WindowSettings;
		//	using namespace serde::attribute;
		//	serde::serde_struct(context, value)
		//		.field(&Self::isFullscreen, "IsFullscreen")
		//		.field(&Self::isCursorVisible, "IsCursorVisible")
		//		.field(&Self::isCursorLock, "IsCursorLock")
		//		.field(&Self::depthBits, "DepthBits")
		//		.field(&Self::stencilBits, "StencilBits")
		//		.field(&Self::antialiasingLevel, "AntialiasingLevel")
		//		.field(&Self::majorVersion, "MajorVersion")
		//		.field(&Self::minorVersion, "MinorVersion")
		//		.field(&Self::title, "Title")
		//		.field(&Self::refreshRate, "RefreshRate")
		//		.field(&Self::size, "Size");
		//}
	};
	
#ifdef USE_SDL
	class  Window {
	public:
		EVENT::Event<int> keyPressedEvent;
		EVENT::Event<int> keyReleasedEvent;
		EVENT::Event<int> mouseButtonPressedEvent;
		EVENT::Event<int> mouseButtonReleasedEvent;
		EVENT::Event<float, float> mouseMovedEvent;

		EVENT::Event<int, INPUT::Gamepad::GAMEPAD_BUTTON> gamepadButtonPressedEvent;
		EVENT::Event<int, INPUT::Gamepad::GAMEPAD_BUTTON> gamepadButtonReleasedEvent;
		EVENT::Event<int, INPUT::Gamepad::GAMEPAD_AXIS, float> gamepadAxisEvent;
		EVENT::Event<int, INPUT::Gamepad::GAMEPAD_TRIGGER, float> gamepadTriggerEvent;

		EVENT::Event<INPUT::Gamepad> gamepadAddEvent;
		EVENT::Event<int> gamepadRemoveEvent;

		explicit Window(const WindowSettings& p_windowSettings, bool isMain = true, Window* sharedWindow = nullptr);
		Window() = delete;
		~Window();

		[[nodiscard]] unsigned int getId() const;
		[[nodiscard]] bool getIsMainWindow() const;

		[[nodiscard]] MATH::Vector2i getMousePos() const;
		void setSize(unsigned int width, unsigned int height);
		[[nodiscard]] MATH::Vector2u getSize() const;
		void setPosition(int x, int y);
		[[nodiscard]] MATH::Vector2i getPosition() const;
		void setTitle(const std::string& title);
		[[nodiscard]] std::string getTitle() const;
		void setDepthBits(int val);
		[[nodiscard]] int getDepathBits() const;
		void setStencilBits(int val);
		[[nodiscard]] int getStencilBits() const;
		void setMajorVersion(int val);
		[[nodiscard]] int getMajorVersion() const;
		void setMinorVersion(int val);
		[[nodiscard]] int getMinorVersion() const;
		void setAntialiasingLevel(int val);
		[[nodiscard]] int getAntialiasingLevel() const;
		void setRefreshRate(int val);
		[[nodiscard]] int getRefreshRate() const;
		void setFullscreen(bool val);
		[[nodiscard]] bool getIsFullscreen() const;
		void toggleFullscreen();

		void hide() const;
		void show() const;
		void focus() const;
		[[nodiscard]] bool hasFocus() const;

		void pollEvent();
		void draw() const;
		void preUpdate();
		void update();
		[[nodiscard]] bool isClosed() const;

		void setCursorVisible(bool isVisible, bool isLock) const;

		std::pair<int, int> getDrawableSize();
		::SDL_Window* getSDLWindow() const;
		void* getGLContext() const;

#ifdef DX12_BACKEND
		HWND mHWND;
		HWND getContextDX12() {
			return mHWND;
		}
#endif

#ifdef VULKAN_BACKEND
		VkSurfaceKHR createVulkanSurface(VkInstance instance);
		std::vector<const char*> getSDLVulkanExtentions();
#endif
#ifdef METAL_BACKEND
		void* getMetalLayer();
#endif

		void initImGUI();
		void shutdownImGUI();
	private:
		
		[[nodiscard]] WindowSettings& getSetting();
		
		void create(Window* sharedWindow = nullptr);
		WindowSettings mWindowSettings;
		struct Internal;
		std::unique_ptr<Internal> mContext;
		bool mIsClose = false;
		bool mIsMainWindow = true;
		unsigned int mWindowID = 0;
	};

#endif

#ifdef OCULUS
	class Window;
	class OxrInput;

    struct AndroidAppState {
        ANativeWindow* NativeWindow = nullptr;
        bool Resumed = false;
        Window* window = nullptr;
    };

    class Window {
        bool renderLayer(XrTime dpy_time,
                               std::vector<XrCompositionLayerProjectionView> &layerViews,
                               XrCompositionLayerProjection                  &layer);
	public:
		EVENT::Event<int> keyPressedEvent;
		EVENT::Event<int> keyReleasedEvent;
		EVENT::Event<int> mouseButtonPressedEvent;
		EVENT::Event<int> mouseButtonReleasedEvent;
		EVENT::Event<float, float> mouseMovedEvent;

		EVENT::Event<int, INPUT::Gamepad::GAMEPAD_BUTTON> gamepadButtonPressedEvent;
		EVENT::Event<int, INPUT::Gamepad::GAMEPAD_BUTTON> gamepadButtonReleasedEvent;
		EVENT::Event<int, INPUT::Gamepad::GAMEPAD_AXIS, float> gamepadAxisEvent;
		EVENT::Event<int, INPUT::Gamepad::GAMEPAD_TRIGGER, float> gamepadTriggerEvent;

		EVENT::Event<INPUT::Gamepad> gamepadAddEvent;
		EVENT::Event<int> gamepadRemoveEvent;

		explicit Window(const WindowSettings& p_windowSettings, android_app* app);
        void init();

		Window() = delete;
		~Window();

		[[nodiscard]] unsigned int getId() const { return 1; }
		[[nodiscard]] bool getIsMainWindow() const { return true; }
		[[nodiscard]] MATH::Vector2i getMousePos() const { return {}; }
		void setSize(unsigned int width, unsigned int height);
		MATH::Vector2u getSize() const;

		[[nodiscard]] bool getIsFullscreen() const { return true; };
		void toggleFullscreen() {};

		void pollEvent();

        void preUpdate();
        void update(std::function<void(XrCompositionLayerProjectionView &layerView,
                                       render_target_t &rtarget, XrPosef &stagePose,
                                       uint32_t viewID)> renderCb);
		void draw() const;
		[[nodiscard]] bool isClosed() const;
		XrSession getXrSession() const { return m_session; }
		XrInstance getXrInstance() const { return m_instance; }
		int32_t onAndroidInput(AInputEvent* event);

	private:

		MATH::Vector2u mSize;

        struct android_app  *m_app = nullptr;

        XrInstance          m_instance;
        XrSession           m_session;
        XrSpace             m_appSpace;
        XrSpace             m_stageSpace;
        XrSystemId          m_systemId;
        std::vector<viewsurface_t> m_viewSurface;

        AndroidAppState appState;

        std::function<void(XrCompositionLayerProjectionView &layerView,
                           render_target_t &rtarget, XrPosef &stagePose,
                           uint32_t viewID)> m_RenderCb;
		std::unique_ptr<OxrInput> mOxrInput;
	};
#endif
}
