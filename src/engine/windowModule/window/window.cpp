#include "window.h"

#ifdef VUKLAN_BACKEND
#include <vulkan/vulkan_core.h>
#include "backends/imgui_impl_vulkan.h"
#endif
#include "renderModule/backends/dx12/d3dUtil.h"
#include "renderModule/backends/dx12/driverDx12.h"
#include "renderModule/backends/vk/driverVk.h"

#ifdef USE_SDL

#include <set>
#include <SDL.h>
#include <SDL_syswm.h>
#include "utilsModule/log/loggerDefine.h"

#include "coreModule/platform.hpp"
#include "windowModule/inputManager/inputManager.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#if defined(USE_EDITOR) || defined(USE_CHEATS)
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"

#ifdef OPENGL_BACKEND
#include "backends/imgui_impl_opengl3.h"
#endif

#ifdef DX12_BACKEND
#include "backends/imgui_impl_dx12.h"
#endif


#endif

using namespace IKIGAI;
using namespace IKIGAI::WINDOW;

const std::map<SDL_GameControllerButton, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON> ToGamepadButton = {
	{SDL_GameControllerButton::SDL_CONTROLLER_BUTTON_A, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_a},
	{SDL_GameControllerButton::SDL_CONTROLLER_BUTTON_B, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_b},
	{SDL_GameControllerButton::SDL_CONTROLLER_BUTTON_X, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_x},
	{SDL_GameControllerButton::SDL_CONTROLLER_BUTTON_Y, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_y},
	{SDL_GameControllerButton::SDL_CONTROLLER_BUTTON_LEFTSTICK, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_leftStick},
	{SDL_GameControllerButton::SDL_CONTROLLER_BUTTON_RIGHTSTICK, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_rightStick},
	{SDL_GameControllerButton::SDL_CONTROLLER_BUTTON_BACK, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_back},
	{SDL_GameControllerButton::SDL_CONTROLLER_BUTTON_START, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_start},
	{SDL_GameControllerButton::SDL_CONTROLLER_BUTTON_LEFTSHOULDER, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_lb},
	{SDL_GameControllerButton::SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::btn_rb},
	{SDL_GameControllerButton::SDL_CONTROLLER_BUTTON_DPAD_UP, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::dpad_up},
	{SDL_GameControllerButton::SDL_CONTROLLER_BUTTON_DPAD_DOWN, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::dpad_down},
	{SDL_GameControllerButton::SDL_CONTROLLER_BUTTON_DPAD_LEFT, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::dpad_left},
	{SDL_GameControllerButton::SDL_CONTROLLER_BUTTON_DPAD_RIGHT, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON::dpad_right},
};

struct Window::Internal {
	Internal() = default;
	SDL_Window* mWindow = nullptr;
	SDL_GLContext mContext;
	bool mIsFocus = true;
	std::set<SDL_GameController*> mGamepads;

	void addGamepad(SDL_GameController* gp) {
		mGamepads.insert(gp);
	}
	void removeGamepad(SDL_GameController* gp) {
		mGamepads.erase(gp);
	}
};

std::vector<SDL_GameController*> findController() {
	std::vector<SDL_GameController*> res;
	for (int i = 0; i < SDL_NumJoysticks(); i++) {
		if (SDL_IsGameController(i)) {
			res.push_back(SDL_GameControllerOpen(i));
		}
	}
	return res;
}

Window::Window(const WindowSettings& p_windowSettings, bool isMain, Window* sharedWindow) : mWindowSettings(p_windowSettings), mContext(std::make_unique<Internal>()), mIsMainWindow(isMain) {
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER);
	create(sharedWindow);

	const auto gamepads = findController();
	for (auto gp : gamepads) {
		int id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gp));
		std::string name = SDL_GameControllerName(gp);
		gamepadAddEvent.run(INPUT::Gamepad(id, name));
	}

}

Window::~Window() {
#ifdef OPENGL_BACKEND
	SDL_GL_DeleteContext(mContext->mContext);
#if defined(USE_EDITOR) || defined(USE_CHEATS)
	ImGui_ImplOpenGL3_Shutdown();
#endif
#endif

#if defined(USE_EDITOR) || defined(USE_CHEATS)
#ifdef DX12_BACKEND
	ImGui_ImplDX12_Shutdown();
#endif
#ifdef VULKAN_BACKEND
	auto driverVk = RENDER::UtilityVk::GetDriver();
	ImGui_ImplVulkan_Shutdown();
	vkDestroyDescriptorPool(*driverVk->mDevice, driverVk->mImguiPool, nullptr);
#endif
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
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
	SDL_GetMouseState(&res.x, &res.y);
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
		SDL_DisplayMode displayMode;
		SDL_GetDesktopDisplayMode(0, &displayMode);
		displayWidth = static_cast<uint32_t>(displayMode.w);
		displayHeight = static_cast<uint32_t>(displayMode.h);
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
	SDL_SetWindowInputFocus(mContext->mWindow);
}

bool Window::hasFocus() const {
	return mContext->mIsFocus;
}

void Window::pollEvent() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
#if defined(USE_EDITOR) || defined(USE_CHEATS)
		ImGui_ImplSDL2_ProcessEvent(&event);
#endif
		switch (event.type) {
		case SDL_QUIT:
			mIsClose = false;
			break;
		case SDL_KEYDOWN:
			keyPressedEvent.run(event.key.keysym.scancode);
			break;
		case SDL_KEYUP:
			keyReleasedEvent.run(event.key.keysym.scancode);
			break;
		case SDL_MOUSEBUTTONUP:
			mouseButtonPressedEvent.run(event.button.button);
			break;
		case SDL_MOUSEBUTTONDOWN:
			mouseButtonReleasedEvent.run(event.button.button);
			break;
		case SDL_MOUSEMOTION:

			break;
		//GAMEPAD
		case SDL_CONTROLLERDEVICEADDED: {
			auto gp = SDL_GameControllerOpen(event.cdevice.which);
			mContext->addGamepad(gp);
			int id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gp));
			std::string name = SDL_GameControllerName(gp);
			gamepadAddEvent.run(INPUT::Gamepad(id, name));
			break;
		}
		case SDL_CONTROLLERDEVICEREMOVED: {
			for (auto gp : mContext->mGamepads) {
				if (gp && event.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gp))) {
					SDL_GameControllerClose(gp);
					mContext->removeGamepad(gp);
					gamepadRemoveEvent.run(event.cdevice.which);
				}
			}
			break;
		}
		case SDL_CONTROLLERBUTTONDOWN: {
			for (auto gp : mContext->mGamepads) {
				int id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gp));
				if (gp && event.cdevice.which == id) {
					gamepadButtonPressedEvent.run(id, ToGamepadButton.at(static_cast<SDL_GameControllerButton>(event.cbutton.button)));
				}
			}
			break;
		}
		case SDL_CONTROLLERBUTTONUP: {
			for (auto gp : mContext->mGamepads) {
				int id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gp));
				if (gp && event.cdevice.which == id) {
					gamepadButtonReleasedEvent.run(id, ToGamepadButton.at(static_cast<SDL_GameControllerButton>(event.cbutton.button)));
				}
			}
			break;
		}
		case SDL_WINDOWEVENT_FOCUS_GAINED:
			mContext->mIsFocus = true;
			break;
		case SDL_WINDOWEVENT_FOCUS_LOST:
			mContext->mIsFocus = false;
			break;
		default:
			break;
		}

		//GAMEPAD
		for (auto gp : mContext->mGamepads) {
			if (gp) {
				const int id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gp));

				static float lxstick = 0.0f;
				static float lystick = 0.0f;
				float x = (float)SDL_GameControllerGetAxis(gp, SDL_GameControllerAxis::SDL_CONTROLLER_AXIS_LEFTX) / (float)INT16_MAX;
				float y = (float)SDL_GameControllerGetAxis(gp, SDL_GameControllerAxis::SDL_CONTROLLER_AXIS_LEFTY) / (float)INT16_MAX;
				if (!MATH::CMP(lxstick, x)) gamepadAxisEvent.run(id, INPUT::Gamepad::GAMEPAD_AXIS::leftStick_X, x);
				if (!MATH::CMP(lystick, y)) gamepadAxisEvent.run(id, INPUT::Gamepad::GAMEPAD_AXIS::leftStick_Y, y);
				lxstick = x;
				lystick = y;

				static float rxstick = 0.0f;
				static float rystick = 0.0f;
				x = (float)SDL_GameControllerGetAxis(gp, SDL_GameControllerAxis::SDL_CONTROLLER_AXIS_RIGHTX) / (float)INT16_MAX;
				y = (float)SDL_GameControllerGetAxis(gp, SDL_GameControllerAxis::SDL_CONTROLLER_AXIS_RIGHTY) / (float)INT16_MAX;
				if (!MATH::CMP(rxstick, x)) gamepadAxisEvent.run(id, INPUT::Gamepad::GAMEPAD_AXIS::rightStick_X, x);
				if (!MATH::CMP(rystick, y)) gamepadAxisEvent.run(id, INPUT::Gamepad::GAMEPAD_AXIS::rightStick_Y, y);
				lxstick = x;
				lystick = y;

				static float rtrigger = 0.0f;
				static float ltrigger = 0.0f;
				x = (float)SDL_GameControllerGetAxis(gp,SDL_GameControllerAxis::SDL_CONTROLLER_AXIS_TRIGGERLEFT) / (float)INT16_MAX;
				y = (float)SDL_GameControllerGetAxis(gp, SDL_GameControllerAxis::SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / (float)INT16_MAX;
				if (!MATH::CMP(rtrigger, x)) gamepadTriggerEvent.run(id, INPUT::Gamepad::GAMEPAD_TRIGGER::leftTrigger, x);
				if (!MATH::CMP(ltrigger, y)) gamepadTriggerEvent.run(id, INPUT::Gamepad::GAMEPAD_TRIGGER::rightTrigger, y);
				rtrigger = x;
				ltrigger = y;
			}
		}
	}
	SDL_GL_MakeCurrent(mContext->mWindow, mContext->mContext);
}

void Window::draw() const {
#if defined(USE_EDITOR) || defined(USE_CHEATS)
#ifdef OPENGL_BACKEND
	ImGuiIO& io = ImGui::GetIO();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		SDL_Window* backup_current_window = SDL_GL_GetCurrentWindow();
		SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
		SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
	}
#endif
#endif
#ifdef OPENGL_BACKEND
	SDL_GL_SwapWindow(mContext->mWindow);
#endif
}

//Call before draw imgui widgets
void Window::preUpdate() {
#if defined(USE_EDITOR) || defined(USE_CHEATS)
#ifdef OPENGL_BACKEND
	ImGui_ImplOpenGL3_NewFrame();
#endif
#ifdef DX12_BACKEND
	ImGui_ImplDX12_NewFrame();
#endif
#ifdef VULKAN_BACKEND
	ImGui_ImplVulkan_NewFrame();
#endif
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
#endif
}

//After call all imgui widgets
void Window::update() {
#if defined(USE_EDITOR) || defined(USE_CHEATS)
	ImGui::Render();
#endif
}

bool Window::isClosed() const {
	return mIsClose;
}

void Window::setCursorVisible(bool isVisible, bool isLock) const {
	SDL_ShowCursor(isVisible ? SDL_ENABLE : SDL_DISABLE);
	SDL_SetRelativeMouseMode(isLock ? SDL_TRUE : SDL_FALSE);
}

std::pair<int, int> Window::getDrawableSize() {
	int viewportWidth;
	int viewportHeight;
	SDL_GL_GetDrawableSize(mContext->mWindow, &viewportWidth, &viewportHeight);
	return { viewportWidth , viewportHeight };
}

#ifdef VULKAN_BACKEND
#include <SDL_vulkan.h>
#include <renderModule/backends/vk/helpers.h>
VkSurfaceKHR Window::createVulkanSurface(VkInstance instance) {
	VkSurfaceKHR surface;
	if (SDL_Vulkan_CreateSurface(mContext->mWindow, instance, &surface) == 0) {
		printf("Failed to create Vulkan surface.\n");
		throw;
	}
	return surface;
}

std::vector<const char*> Window::getSDLVulkanExtentions() {
	std::vector<const char*> extensions;
	uint32_t extensions_count = 0;
	SDL_Vulkan_GetInstanceExtensions(mContext->mWindow, &extensions_count, nullptr);
	extensions.resize(extensions_count);
	SDL_Vulkan_GetInstanceExtensions(mContext->mWindow, &extensions_count, extensions.data());
	return extensions;
}
#endif

void Window::initImGUI() {
#if defined(USE_EDITOR) || defined(USE_CHEATS)

#ifdef __EMSCRIPTEN__
	// GL ES 2.0 + GLSL 100
	const char* glsl_version = "#version 100";
#elif defined(__APPLE__)
	// GL 3.2 Core + GLSL 150
	const char* glsl_version = "#version 150";
#elif defined(__ANDROID__)
	const char* glsl_version = "#version 100";
#else
	// GL 3.0 + GLSL 130
	const char* glsl_version = "#version 130";
#endif

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
	//io.ConfigViewportsNoAutoMerge = true;
	//io.ConfigViewportsNoTaskBarIcon = true;

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();

	// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
	ImGuiStyle& style = ImGui::GetStyle();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	// TODO: This need init after create render
	// Setup Platform/Renderer backends
#ifdef OPENGL_BACKEND
	ImGui_ImplSDL2_InitForOpenGL(mContext->mWindow, mContext->mContext);
	ImGui_ImplOpenGL3_Init("#version 330");
#endif

#ifdef DX12_BACKEND
	ImGui_ImplSDL2_InitForD3D(mContext->mWindow);
	ImGui_ImplDX12_Init(RENDER::d3dUtil::GetDriver()->getDevice().Get(), 
		RENDER::DriverDx12::DEFAULT_FB_SIZE,
		DXGI_FORMAT_R8G8B8A8_UNORM, RENDER::d3dUtil::GetDriver()->getDescriptorHeap().Get(),
		RENDER::d3dUtil::GetDriver()->getDescriptorHeapCPUHandle(),
		RENDER::d3dUtil::GetDriver()->getDescriptorHeapGPUHandle());
	RENDER::d3dUtil::GetDriver()->getDescriptorHeapCPUHandle().Offset(1, RENDER::d3dUtil::GetDriver()->getDescriptorIncSize());
	RENDER::d3dUtil::GetDriver()->getDescriptorHeapGPUHandle().Offset(1, RENDER::d3dUtil::GetDriver()->getDescriptorIncSize());
#endif

#ifdef VULKAN_BACKEND
	ImGui_ImplSDL2_InitForVulkan(mContext->mWindow);

	auto driverVk = RENDER::UtilityVk::GetDriver();

	VkDescriptorPoolSize pool_sizes[] = {
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
	VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = 1000,
		.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes)),
		.pPoolSizes = pool_sizes
	};
	
	(vkCreateDescriptorPool(*driverVk->mDevice, &pool_info, nullptr, &driverVk->mImguiPool));

	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = *driverVk->mInstance;
	init_info.PhysicalDevice = *driverVk->mPhysicalDevice;
	init_info.Device = *driverVk->mDevice;
	init_info.QueueFamily = driverVk->mQueueFamilyIndex;
	init_info.Queue = *driverVk->mQueue;
	init_info.DescriptorPool = driverVk->mImguiPool;
	init_info.Subpass = 0;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	init_info.UseDynamicRendering = true;

	static auto _swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;
	init_info.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchainImageFormat;
	init_info.PipelineRenderingCreateInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
	init_info.PipelineRenderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;

	ImGui_ImplVulkan_LoadFunctions([](const char* functionName, void* vulkanInstance) {
		if (strcmp("vkCmdBeginRenderingKHR", functionName) == 0) {
			return vkGetInstanceProcAddr(*(reinterpret_cast<VkInstance*>(vulkanInstance)), "vkCmdBeginRendering");
		}
		if (strcmp("vkCmdEndRenderingKHR", functionName) == 0) {
			return vkGetInstanceProcAddr(*(reinterpret_cast<VkInstance*>(vulkanInstance)), "vkCmdEndRendering");
		}
		return vkGetInstanceProcAddr(*(reinterpret_cast<VkInstance*>(vulkanInstance)), functionName);
	}, &init_info.Instance);

	ImGui_ImplVulkan_Init(&init_info);
	ImGui_ImplVulkan_CreateFontsTexture();
#endif

#endif
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

#ifdef OPENGL_BACKEND
#ifdef __EMSCRIPTEN__
	// GL ES 3.0 + GLSL 300
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
	// GL 3.2 Core + GLSL 150
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#elif defined(__ANDROID__)
	// GL 3.2 Core + GLSL 150
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
	// GL 3.0 + GLSL 130
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
#endif


	SDL_WindowFlags flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN);
#ifdef OPENGL_BACKEND
	flags = (SDL_WindowFlags)(flags | SDL_WINDOW_OPENGL);
#endif
#ifdef VULKAN_BACKEND
	flags = (SDL_WindowFlags)(flags | SDL_WINDOW_VULKAN);
#endif
#ifdef METAL_BACKEND
	flags = (SDL_WindowFlags)(flags | SDL_WINDOW_METAL);
#endif
	SDL_Window* _window{
		SDL_CreateWindow(mWindowSettings.title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, displaySize.x, displaySize.y,flags)
	};

	if (_window == nullptr) {
		//TODO:
		//printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
		//return -1;
	}

#ifdef DX12_BACKEND
	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);
	SDL_GetWindowWMInfo(_window, &wmInfo);
	mHWND = (HWND)wmInfo.info.win.window;
#endif

	if (::shouldDisplayFullScreen() || mWindowSettings.isFullscreen) {
		mWindowSettings.isFullscreen = true;
		SDL_SetWindowFullscreen(_window, SDL_TRUE);
	}
	mContext->mWindow = _window;
	mWindowID = SDL_GetWindowID(_window);
#ifdef OPENGL_BACKEND
	if (sharedWindow) {
		SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
		SDL_GL_MakeCurrent(sharedWindow->mContext->mWindow, sharedWindow->mContext->mContext);
	}
	mContext->mContext = SDL_GL_CreateContext(mContext->mWindow);
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
