#include "window.h"

#ifdef USE_SDL

#include <set>
#include <SDL3/SDL.h>
#include "utilsModule/log/loggerDefine.h"
#include "coreModule/platform.hpp"
#include "windowModule/inputManager/inputManager.h"
#ifdef __EMSCRIPTEN__
#include <iostream>
#include <emscripten.h>
#endif

#if defined(USE_EDITOR) || defined(USE_CHEATS)
#include "imgui.h"
#include "utilsModule/imguiHelper/imguiBackend/imguiBackend.h"
#endif

#ifdef VULKAN_BACKEND
#include <SDL3/SDL_vulkan.h>
#endif
#ifdef METAL_BACKEND
#include <SDL3/SDL_metal.h>
#endif

#ifdef DX12_BACKEND
#include <Windows.h>
#endif

using namespace IKIGAI;
using namespace IKIGAI::WINDOW;

const std::map<SDL_GamepadButton, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON> ToGamepadButton = {
	{SDL_GAMEPAD_BUTTON_SOUTH, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_a},
	{SDL_GAMEPAD_BUTTON_EAST, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_b},
	{SDL_GAMEPAD_BUTTON_WEST, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_x},
	{SDL_GAMEPAD_BUTTON_NORTH, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_y},
	{SDL_GAMEPAD_BUTTON_LEFT_STICK, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_leftStick},
	{SDL_GAMEPAD_BUTTON_RIGHT_STICK, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_rightStick},
	{SDL_GAMEPAD_BUTTON_BACK, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_back},
	{SDL_GAMEPAD_BUTTON_START, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_start},
	{SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_lb},
	{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_rb},
	{SDL_GAMEPAD_BUTTON_DPAD_UP, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::dpad_up},
	{SDL_GAMEPAD_BUTTON_DPAD_DOWN, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::dpad_down},
	{SDL_GAMEPAD_BUTTON_DPAD_LEFT, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::dpad_left},
	{SDL_GAMEPAD_BUTTON_DPAD_RIGHT, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::dpad_right},
};

struct Window::Internal {
	Internal() = default;
	::SDL_Window* mWindow = nullptr;
	SDL_GLContext mContext = nullptr;
	bool mIsFocus = true;
	std::set<SDL_Gamepad*> mGamepads;
#if defined(USE_EDITOR) || defined(USE_CHEATS)
	std::unique_ptr<IMGUI::IImGuiBackend> mImGuiBackend;
#endif

	void addGamepad(SDL_Gamepad* gp) {
		mGamepads.insert(gp);
	}
	void removeGamepad(SDL_Gamepad* gp) {
		mGamepads.erase(gp);
	}
};

std::vector<SDL_Gamepad*> findController() {
	std::vector<SDL_Gamepad*> res;
	int count = 0;
	SDL_JoystickID* ids = SDL_GetGamepads(&count);
	if (ids) {
		for (int i = 0; i < count; ++i) {
			if (SDL_Gamepad* gp = SDL_OpenGamepad(ids[i])) {
				res.push_back(gp);
			}
		}
		SDL_free(ids);
	}
	return res;
}

Window::Window(const WindowSettings& p_windowSettings, bool isMain, Window* sharedWindow) : mWindowSettings(p_windowSettings), mContext(std::make_unique<Internal>()), mIsMainWindow(isMain) {
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD);
	create(sharedWindow);

	const auto gamepads = findController();
	for (auto gp : gamepads) {
		int id = static_cast<int>(SDL_GetGamepadID(gp));
		std::string name = SDL_GetGamepadName(gp) ? SDL_GetGamepadName(gp) : "";
		gamepadAddEvent.run(INPUT::Gamepad(id, name));
	}

}

Window::~Window() {
	shutdownImGUI();
#ifdef OPENGL_BACKEND
	if (mWindowSettings.renderBackend == RENDER::RenderSettings::Backend::OPENGL && mContext->mContext) {
		SDL_GL_DestroyContext(mContext->mContext);
		mContext->mContext = nullptr;
	}
#endif

	if (mIsMainWindow) {
		SDL_DestroyWindow(mContext->mWindow);
		SDL_Quit();
	} else {
		SDL_DestroyWindow(mContext->mWindow);
	}
}

unsigned int Window::getId() const {
	return mWindowID;
}

bool Window::getIsMainWindow() const {
	return mIsMainWindow;
}

MATH::Vector2i Window::getMousePos() const {
	MATH::Vector2i res;
	float x = 0.0f;
	float y = 0.0f;
	SDL_GetMouseState(&x, &y);
	res.x = static_cast<int>(x);
	res.y = static_cast<int>(y);
	return res;
}

void Window::setSize(unsigned width, unsigned height) {
	switch (GetCurrentPlatform()) {
		case Platform::IOS:
		case Platform::ANDROIDOS:
		case Platform::EMSCRIPT: {
			//Can not change window size
			break;
		}
		case Platform::WINDOWS:
		case Platform::MAC: {
			mWindowSettings.size = { width, height };
			SDL_SetWindowSize(mContext->mWindow, width, height);
		}
		default: {
			break;
		}
	}
}

MATH::Vector2u Window::getSize() const
{
	uint32_t displayWidth{ 0 };
	uint32_t displayHeight{ 0 };

#ifdef __EMSCRIPTEN__
	// For Emscripten targets we will invoke some Javascript
	// to find out the dimensions of the canvas in the HTML
	// document. Note that the 'width' and 'height' attributes
	// need to be set on the <canvas /> HTML element, like so:
	// <canvas id="canvas" width="600", height="360"></canvas>
	displayWidth = static_cast<uint32_t>(EM_ASM_INT({
		return document.getElementById('canvas').width;
	}));

	displayHeight = static_cast<uint32_t>(EM_ASM_INT({
		return document.getElementById('canvas').height;
	}));
#else
	switch (GetCurrentPlatform()) {
	case Platform::IOS:
	case Platform::ANDROIDOS: {
		// For mobile platforms we will fetch the full screen size.
		const SDL_DisplayMode* displayMode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
		if (displayMode) {
			displayWidth = static_cast<uint32_t>(displayMode->w);
			displayHeight = static_cast<uint32_t>(displayMode->h);
		}
		break;
	}
	default: {
		// For other platforms we'll just show a fixed size window.
		displayWidth = mWindowSettings.size.x;
		displayHeight = mWindowSettings.size.y;
		break;
	}
	}
#endif

	return MATH::Vector2u(displayWidth, displayHeight);
}

void Window::setPosition(int x, int y) {
	SDL_SetWindowPosition(mContext->mWindow, x, y);
}

MATH::Vector2i Window::getPosition() const {
	MATH::Vector2i res;
	SDL_GetWindowPosition(mContext->mWindow, &res.x, &res.y);
	return res;
}

void Window::setTitle(const std::string& title) {
	SDL_SetWindowTitle(mContext->mWindow, title.c_str());
	mWindowSettings.title = title;
}

std::string Window::getTitle() const {
	return SDL_GetWindowTitle(mContext->mWindow);
}

void Window::setDepthBits(int val) {
	if (mContext->mWindow) return;
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, val);
	mWindowSettings.depthBits = val;
}

int Window::getDepathBits() const {
	int res = 0;
	SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &res);
	return res;
}

void Window::setStencilBits(int val) {
	if (mContext->mWindow) return;
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, val);
	mWindowSettings.stencilBits = val;
}

int Window::getStencilBits() const {
	int res = 0;
	SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &res);
	return res;
}

void Window::setMajorVersion(int val) {
	if (mContext->mWindow) return;
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, val);
	mWindowSettings.majorVersion = val;
}

int Window::getMajorVersion() const {
	int res = 0;
	SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &res);
	return res;
}

void Window::setMinorVersion(int val) {
	if (mContext->mWindow) return;
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, val);
	mWindowSettings.minorVersion = val;
}

int Window::getMinorVersion() const {
	int res = 0;
	SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &res);
	return res;
}

void Window::setAntialiasingLevel(int val) {
	if (mContext->mWindow) return;
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, val);
	mWindowSettings.antialiasingLevel = val;
}

int Window::getAntialiasingLevel() const {
	int res = 0;
	SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &res);
	return res;
}

void Window::setRefreshRate(int val) {
	
}

int Window::getRefreshRate() const {
	return 0;
}

void Window::setFullscreen(bool val) {
	switch (GetCurrentPlatform()) {
	case Platform::IOS:
	case Platform::ANDROIDOS: {
		//Can not change for this platform
		return;
	}
	case Platform::EMSCRIPT:
	case Platform::WINDOWS:
	case Platform::MAC:
	default: {
		break;
	}
	}
	mWindowSettings.isFullscreen = val;
	SDL_SetWindowFullscreen(mContext->mWindow, mWindowSettings.isFullscreen);
}

bool Window::getIsFullscreen() const {
	switch (GetCurrentPlatform()) {
		case Platform::IOS:
		case Platform::ANDROIDOS: {
			return true;
		}
		case Platform::EMSCRIPT:
		case Platform::WINDOWS:
		case Platform::MAC:
		default: {
			break;
		}
	}
	return mWindowSettings.isFullscreen;
}

void Window::toggleFullscreen() {
	switch (GetCurrentPlatform()) {
		case Platform::IOS:
		case Platform::ANDROIDOS: {
			//Can not change for this platform
			return;
		}
		case Platform::EMSCRIPT:
		case Platform::WINDOWS:
		case Platform::MAC:
		default: {
			break;
		}
	}
	mWindowSettings.isFullscreen = !mWindowSettings.isFullscreen;
	SDL_SetWindowFullscreen(mContext->mWindow, mWindowSettings.isFullscreen);
}

void Window::hide() const {
	SDL_HideWindow(mContext->mWindow);
}

void Window::show() const {
	SDL_ShowWindow(mContext->mWindow);
}

void Window::focus() const {
	SDL_RaiseWindow(mContext->mWindow);
}

bool Window::hasFocus() const {
	return mContext->mIsFocus;
}

void Window::pollEvent() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
#if defined(USE_EDITOR) || defined(USE_CHEATS)
		if (mContext->mImGuiBackend) {
			mContext->mImGuiBackend->processEvent(&event);
		}
#endif
		switch (event.type) {
		case SDL_EVENT_QUIT:
			mIsClose = false;
			break;
		case SDL_EVENT_KEY_DOWN:
#ifdef __EMSCRIPTEN__
			{
				static int keyLogs = 8;
				if (keyLogs > 0) {
					--keyLogs;
					std::cout << "[web] key down scancode=" << static_cast<int>(event.key.scancode) << std::endl;
					std::cout.flush();
				}
			}
#endif
			keyPressedEvent.run(event.key.scancode);
			break;
		case SDL_EVENT_KEY_UP:
			keyReleasedEvent.run(event.key.scancode);
			break;
		case SDL_EVENT_MOUSE_BUTTON_UP:
			mouseButtonPressedEvent.run(event.button.button);
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			mouseButtonReleasedEvent.run(event.button.button);
			break;
		case SDL_EVENT_MOUSE_MOTION:
			mouseMovedEvent.run(event.motion.xrel, event.motion.yrel);
			break;
		case SDL_EVENT_GAMEPAD_ADDED: {
			auto gp = SDL_OpenGamepad(event.gdevice.which);
			if (!gp) {
				break;
			}
			mContext->addGamepad(gp);
			int id = static_cast<int>(SDL_GetGamepadID(gp));
			std::string name = SDL_GetGamepadName(gp) ? SDL_GetGamepadName(gp) : "";
			gamepadAddEvent.run(INPUT::Gamepad(id, name));
			break;
		}
		case SDL_EVENT_GAMEPAD_REMOVED: {
			for (auto gp : mContext->mGamepads) {
				if (gp && event.gdevice.which == SDL_GetGamepadID(gp)) {
					SDL_CloseGamepad(gp);
					mContext->removeGamepad(gp);
					gamepadRemoveEvent.run(static_cast<int>(event.gdevice.which));
					break;
				}
			}
			break;
		}
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
			for (auto gp : mContext->mGamepads) {
				int id = static_cast<int>(SDL_GetGamepadID(gp));
				if (gp && event.gbutton.which == SDL_GetGamepadID(gp)) {
					gamepadButtonPressedEvent.run(id, ToGamepadButton.at(static_cast<SDL_GamepadButton>(event.gbutton.button)));
				}
			}
			break;
		}
		case SDL_EVENT_GAMEPAD_BUTTON_UP: {
			for (auto gp : mContext->mGamepads) {
				int id = static_cast<int>(SDL_GetGamepadID(gp));
				if (gp && event.gbutton.which == SDL_GetGamepadID(gp)) {
					gamepadButtonReleasedEvent.run(id, ToGamepadButton.at(static_cast<SDL_GamepadButton>(event.gbutton.button)));
				}
			}
			break;
		}
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			mContext->mIsFocus = true;
			break;
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			mContext->mIsFocus = false;
			break;
		default:
			break;
		}

		for (auto gp : mContext->mGamepads) {
			if (gp) {
				const int id = static_cast<int>(SDL_GetGamepadID(gp));

				static float lxstick = 0.0f;
				static float lystick = 0.0f;
				float x = (float)SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX) / (float)INT16_MAX;
				float y = (float)SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY) / (float)INT16_MAX;
				if (!MATH::CMP(lxstick, x)) gamepadAxisEvent.run(id, INPUT::Gamepad::GAMEPAD_AXIS::leftStick_X, x);
				if (!MATH::CMP(lystick, y)) gamepadAxisEvent.run(id, INPUT::Gamepad::GAMEPAD_AXIS::leftStick_Y, y);
				lxstick = x;
				lystick = y;

				static float rxstick = 0.0f;
				static float rystick = 0.0f;
				x = (float)SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTX) / (float)INT16_MAX;
				y = (float)SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTY) / (float)INT16_MAX;
				if (!MATH::CMP(rxstick, x)) gamepadAxisEvent.run(id, INPUT::Gamepad::GAMEPAD_AXIS::rightStick_X, x);
				if (!MATH::CMP(rystick, y)) gamepadAxisEvent.run(id, INPUT::Gamepad::GAMEPAD_AXIS::rightStick_Y, y);
				rxstick = x;
				rystick = y;

				static float rtrigger = 0.0f;
				static float ltrigger = 0.0f;
				x = (float)SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / (float)INT16_MAX;
				y = (float)SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / (float)INT16_MAX;
				if (!MATH::CMP(ltrigger, x)) gamepadTriggerEvent.run(id, INPUT::Gamepad::GAMEPAD_TRIGGER::leftTrigger, x);
				if (!MATH::CMP(rtrigger, y)) gamepadTriggerEvent.run(id, INPUT::Gamepad::GAMEPAD_TRIGGER::rightTrigger, y);
				ltrigger = x;
				rtrigger = y;
			}
		}
	}
	if (mWindowSettings.renderBackend == RENDER::RenderSettings::Backend::OPENGL) {
		SDL_GL_MakeCurrent(mContext->mWindow, mContext->mContext);
	}
}

void Window::draw() const {
#if defined(USE_EDITOR) || defined(USE_CHEATS)
	if (mWindowSettings.renderBackend == RENDER::RenderSettings::Backend::OPENGL && mContext->mImGuiBackend) {
		mContext->mImGuiBackend->renderDrawData();
	}
#endif
	if (mWindowSettings.renderBackend == RENDER::RenderSettings::Backend::OPENGL) {
		SDL_GL_SwapWindow(mContext->mWindow);
	}
}

//Call before draw imgui widgets
void Window::preUpdate() {
#if defined(USE_EDITOR) || defined(USE_CHEATS)
	if (mContext->mImGuiBackend) {
		mContext->mImGuiBackend->newFrame();
	}
#endif
}

void Window::update() {
#if defined(USE_EDITOR) || defined(USE_CHEATS)
	if (mContext->mImGuiBackend) {
		mContext->mImGuiBackend->endFrame();
	}
#endif
}

bool Window::isClosed() const {
	return mIsClose;
}

void Window::setCursorVisible(bool isVisible, bool isLock) const {
	if (isVisible) {
		SDL_ShowCursor();
	} else {
		SDL_HideCursor();
	}
	SDL_SetWindowRelativeMouseMode(mContext->mWindow, isLock);
}

std::pair<int, int> Window::getDrawableSize() {
	int viewportWidth = 0;
	int viewportHeight = 0;
	SDL_GetWindowSizeInPixels(mContext->mWindow, &viewportWidth, &viewportHeight);
	return { viewportWidth , viewportHeight };
}

#ifdef VULKAN_BACKEND
VkSurfaceKHR Window::createVulkanSurface(VkInstance instance) {
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	if (!SDL_Vulkan_CreateSurface(mContext->mWindow, instance, nullptr, &surface)) {
		printf("Failed to create Vulkan surface.\n");
		throw;
	}
	return surface;
}

std::vector<const char*> Window::getSDLVulkanExtentions() {
	std::vector<const char*> extensions;
	Uint32 extensions_count = 0;
	const char* const* names = SDL_Vulkan_GetInstanceExtensions(&extensions_count);
	if (names) {
		extensions.assign(names, names + extensions_count);
	}
	return extensions;
}
#endif

void Window::initImGUI() {
#if defined(USE_EDITOR) || defined(USE_CHEATS)
	if (!RENDER::DriverInterface::Get()) {
		return;
	}
	mContext->mImGuiBackend = IMGUI::CreateImGuiBackend(mWindowSettings.renderBackend);
	if (mContext->mImGuiBackend && mContext->mImGuiBackend->init(*this, *RENDER::DriverInterface::Get())) {
		IMGUI::SetActive(mContext->mImGuiBackend.get());
	}
#endif
}

void Window::shutdownImGUI() {
#if defined(USE_EDITOR) || defined(USE_CHEATS)
	if (mContext && mContext->mImGuiBackend) {
		if (IMGUI::Get() == mContext->mImGuiBackend.get()) {
			IMGUI::SetActive(nullptr);
		}
		mContext->mImGuiBackend->shutdown();
		mContext->mImGuiBackend.reset();
	}
#endif
}

::SDL_Window* Window::getSDLWindow() const {
	return mContext->mWindow;
}

void* Window::getGLContext() const {
	return mContext->mContext;
}

#ifdef METAL_BACKEND
void* Window::getMetalLayer() {
	static SDL_MetalView view = nullptr;
    if (!view) {
        view = SDL_Metal_CreateView(mContext->mWindow);
    }
    return SDL_Metal_GetLayer(view);
}
#endif

WindowSettings& Window::getSetting() {
	return mWindowSettings;
}

bool shouldDisplayFullScreen() {
	switch (GetCurrentPlatform())
	{
	case Platform::IOS:
	case Platform::ANDROIDOS:
		return true;
	default:
		return false;
	}
}

void Window::create(Window* sharedWindow) {
	auto displaySize = getSize();
	const auto backend = mWindowSettings.renderBackend;

#ifdef OPENGL_BACKEND
	if (backend == RENDER::RenderSettings::Backend::OPENGL) {
#ifdef __EMSCRIPTEN__
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#elif defined(__ANDROID__)
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 8);
	}
#endif

	SDL_WindowFlags flags = static_cast<SDL_WindowFlags>(SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	if (backend == RENDER::RenderSettings::Backend::OPENGL) {
		flags = static_cast<SDL_WindowFlags>(flags | SDL_WINDOW_OPENGL);
	} else if (backend == RENDER::RenderSettings::Backend::VULKAN) {
		flags = static_cast<SDL_WindowFlags>(flags | SDL_WINDOW_VULKAN);
	} else if (backend == RENDER::RenderSettings::Backend::METAL) {
		flags = static_cast<SDL_WindowFlags>(flags | SDL_WINDOW_METAL);
	}
	SDL_Window* _window{
		SDL_CreateWindow(mWindowSettings.title.c_str(), static_cast<int>(displaySize.x), static_cast<int>(displaySize.y), flags)
	};

	if (_window == nullptr) {
		//TODO:
		//printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
		//return -1;
	} else {
		SDL_SetWindowPosition(_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	}

#ifdef DX12_BACKEND
	if (backend == RENDER::RenderSettings::Backend::DIRECTX12 && _window) {
		SDL_PropertiesID props = SDL_GetWindowProperties(_window);
		mHWND = static_cast<HWND>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
	}
#endif

	if (::shouldDisplayFullScreen() || mWindowSettings.isFullscreen) {
		mWindowSettings.isFullscreen = true;
		SDL_SetWindowFullscreen(_window, true);
	}
	mContext->mWindow = _window;
	mWindowID = SDL_GetWindowID(_window);
#ifdef OPENGL_BACKEND
	if (backend == RENDER::RenderSettings::Backend::OPENGL) {
		if (sharedWindow) {
			SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
			SDL_GL_MakeCurrent(sharedWindow->mContext->mWindow, sharedWindow->mContext->mContext);
		}
		mContext->mContext = SDL_GL_CreateContext(mContext->mWindow);
	}
#endif
#ifdef METAL_BACKEND
	// mMetalLayer can be stored in Window::Internal if we want, or created on the fly.
	// But SDL_Metal_CreateView is already called in getMetalLayer if needed, or we just call it once here.
#endif
	//initImGUI();
}
#endif

#ifdef OCULUS
using namespace IKIGAI;
using namespace IKIGAI::WINDOW;

static void ProcessAndroidCmd (struct android_app* app, int32_t cmd) {
    AndroidAppState* appState = (AndroidAppState*)app->userData;

    switch (cmd) {
        case APP_CMD_START:
            LOGI ("APP_CMD_START");
            break;

        case APP_CMD_RESUME:
            LOGI ("APP_CMD_RESUME");
            appState->Resumed = true;
            break;

        case APP_CMD_PAUSE:
            LOGI ("APP_CMD_PAUSE");
            appState->Resumed = false;
            break;

        case APP_CMD_STOP:
            LOGI ("APP_CMD_STOP");
            break;

        case APP_CMD_DESTROY:
            LOGI ("APP_CMD_DESTROY");
            appState->NativeWindow = NULL;
            break;

            // The window is being shown, get it ready.
        case APP_CMD_INIT_WINDOW:
            LOGI ("APP_CMD_INIT_WINDOW");
            appState->NativeWindow = app->window;
            break;

            // The window is being hidden or closed, clean it up.
        case APP_CMD_TERM_WINDOW:
            LOGI ("APP_CMD_TERM_WINDOW");
            appState->NativeWindow = NULL;
            break;
    }
}

Window::Window(const WindowSettings &p_windowSettings, android_app *app): m_app(app) {
    app->userData = &appState;
    app->onAppCmd = ProcessAndroidCmd;

    init();
}

void Window::init()
{
    void *vm    = m_app->activity->vm;
    void *clazz = m_app->activity->clazz;

    oxr_initialize_loader (vm, clazz);

    m_instance = oxr_create_instance (vm, clazz);
    m_systemId = oxr_get_system (m_instance);

    egl_init_with_pbuffer_surface (3, 24, 0, 0, 16, 16);
    oxr_confirm_gfx_requirements (m_instance, m_systemId);

    m_session    = oxr_create_session (m_instance, m_systemId);
    m_appSpace   = oxr_create_ref_space (m_session, XR_REFERENCE_SPACE_TYPE_LOCAL);
    m_stageSpace = oxr_create_ref_space (m_session, XR_REFERENCE_SPACE_TYPE_STAGE);

    m_viewSurface = oxr_create_viewsurface (m_instance, m_systemId, m_session);
}

void Window::pollEvent() {
    //TODO: send event to input system

    // Read all pending events.
    for (;;) {
        int events;
        struct android_poll_source* source;

        int timeout = -1; // blocking
        if (appState.Resumed || oxr_is_session_running() || m_app->destroyRequested)
            timeout = 0;  // non blocking

        if (ALooper_pollAll(timeout, nullptr, &events, (void**)&source) < 0) {
            break;
        }

        if (source != nullptr) {
            source->process(m_app, source);
        }
    }
}

bool Window::isClosed() const {
    return m_app->destroyRequested != 0;
}

MATH::Vector2u Window::getSize() const {
    return mSize;
}

void Window::setSize(unsigned int width, unsigned int height) {
    mSize.x = width;
    mSize.y = height;
}

void Window::preUpdate() {
    bool exit_loop, req_restart;
    oxr_poll_events (m_instance, m_session, &exit_loop, &req_restart);

    if (!oxr_is_session_running()) {
        return;
    }
}

void Window::update(std::function<void(XrCompositionLayerProjectionView &layerView,
                                       render_target_t &rtarget, XrPosef &stagePose,
                                       uint32_t viewID)> renderCb) {
    m_RenderCb = renderCb;
    std::vector<XrCompositionLayerBaseHeader*> all_layers;

    XrTime dpy_time;
    oxr_begin_frame (m_session, &dpy_time);

    std::vector<XrCompositionLayerProjectionView> projLayerViews;
    XrCompositionLayerProjection                  projLayer;
    renderLayer(dpy_time, projLayerViews, projLayer);

    all_layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&projLayer));

    /* Compose all layers */
    oxr_end_frame (m_session, dpy_time, all_layers);
}

void Window::draw() const {

}

bool Window::renderLayer(XrTime dpy_time,
                       std::vector<XrCompositionLayerProjectionView> &layerViews,
                       XrCompositionLayerProjection                  &layer)
{
    /* Acquire View Location */
    uint32_t viewCount = (uint32_t)m_viewSurface.size();

    std::vector<XrView> views(viewCount, {XR_TYPE_VIEW});
    oxr_locate_views (m_session, dpy_time, m_appSpace, &viewCount, views.data());

    layerViews.resize (viewCount);

    /* Acquire Stage Location (rerative to the View Location) */
    XrSpaceLocation stageLoc {XR_TYPE_SPACE_LOCATION};
    xrLocateSpace (m_stageSpace, m_appSpace, dpy_time, &stageLoc);


    /* Render each view */
    for (uint32_t i = 0; i < viewCount; i++) {
        XrSwapchainSubImage subImg;
        render_target_t     rtarget;

        oxr_acquire_viewsurface (m_viewSurface[i], rtarget, subImg);

        layerViews[i] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        layerViews[i].pose     = views[i].pose;
        layerViews[i].fov      = views[i].fov;
        layerViews[i].subImage = subImg;

        m_RenderCb(layerViews[i], rtarget, stageLoc.pose, i);

        oxr_release_viewsurface (m_viewSurface[i]);
    }
    layer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer.space     = m_appSpace;
    layer.viewCount = (uint32_t)layerViews.size();
    layer.views     = layerViews.data();

    return true;
}

#endif
