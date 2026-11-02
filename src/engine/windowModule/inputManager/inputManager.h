#pragma once

#include "../window/window.h"
#include "inputGestures.h"
#include <chrono>
#include <unordered_set>
#include <utilsModule/event.h>
#include <vector>

namespace IKIGAI::INPUT_SYSTEM {
#ifdef USE_SDL
enum class EKey {
	KEY_UNKNOWN = 0,
	KEY_SPACE = 44,
	KEY_APOSTROPHE = 49,
	KEY_COMMA = 54,
	KEY_MINUS = 45,
	KEY_PERIOD = 55,
	KEY_SLASH = 56,
	KEY_0 = 30,
	KEY_1 = 31,
	KEY_2 = 32,
	KEY_3 = 33,
	KEY_4 = 34,
	KEY_5 = 35,
	KEY_6 = 36,
	KEY_7 = 37,
	KEY_8 = 38,
	KEY_9 = 39,
	KEY_SEMICOLON = 51,
	KEY_EQUAL = 46,
	KEY_A = 4,
	KEY_B = 5,
	KEY_C = 6,
	KEY_D = 7,
	KEY_E = 8,
	KEY_F = 9,
	KEY_G = 10,
	KEY_H = 11,
	KEY_I = 12,
	KEY_J = 13,
	KEY_K = 14,
	KEY_L = 15,
	KEY_M = 16,
	KEY_N = 17,
	KEY_O = 18,
	KEY_P = 19,
	KEY_Q = 20,
	KEY_R = 21,
	KEY_S = 22,
	KEY_T = 23,
	KEY_U = 24,
	KEY_V = 25,
	KEY_W = 26,
	KEY_X = 27,
	KEY_Y = 28,
	KEY_Z = 29,
	KEY_LEFT_BRACKET = 47,
	KEY_BACKSLASH = 49,
	KEY_RIGHT_BRACKET = 48,
	KEY_GRAVE_ACCENT = 53,
	// KEY_WORLD_1 = 61,//?
	// KEY_WORLD_2 = 62,//?
	KEY_ESCAPE = 41,
	KEY_ENTER = 40,
	KEY_TAB = 43,
	KEY_BACKSPACE = 42,
	KEY_INSERT = 73,
	KEY_DELETE = 76,
	KEY_RIGHT = 79,
	KEY_LEFT = 80,
	KEY_DOWN = 81,
	KEY_UP = 82,

	KEY_PAGE_UP = 75,
	KEY_PAGE_DOWN = 78,
	KEY_HOME = 74,
	KEY_END = 77,
	KEY_CAPS_LOCK = 57,
	KEY_SCROLL_LOCK = 71,
	KEY_NUM_LOCK = 83,
	KEY_PRINT_SCREEN = 70,
	KEY_PAUSE = 72,
	KEY_F1 = 58,
	KEY_F2 = 59,
	KEY_F3 = 60,
	KEY_F4 = 61,
	KEY_F5 = 62,
	KEY_F6 = 63,
	KEY_F7 = 64,
	KEY_F8 = 65,
	KEY_F9 = 66,
	KEY_F10 = 67,
	KEY_F11 = 68,
	KEY_F12 = 69,
	KEY_F13 = 104,
	KEY_F14 = 105,
	KEY_F15 = 106,
	KEY_F16 = 107,
	KEY_F17 = 108,
	KEY_F18 = 109,
	KEY_F19 = 110,
	KEY_F20 = 111,
	KEY_F21 = 112,
	KEY_F22 = 113,
	KEY_F23 = 114,
	KEY_F24 = 115,
	// KEY_F25 = 314,
	KEY_KP_0 = 98,
	KEY_KP_1 = 89,
	KEY_KP_2 = 90,
	KEY_KP_3 = 91,
	KEY_KP_4 = 92,
	KEY_KP_5 = 93,
	KEY_KP_6 = 94,
	KEY_KP_7 = 95,
	KEY_KP_8 = 96,
	KEY_KP_9 = 97,
	KEY_KP_DECIMAL = 220,
	KEY_KP_DIVIDE = 84,
	KEY_KP_MULTIPLY = 85,
	KEY_KP_SUBTRACT = 86,
	KEY_KP_ADD = 87,
	KEY_KP_ENTER = 88,
	KEY_KP_EQUAL = 103,
	KEY_LEFT_SHIFT = 225,
	KEY_LEFT_CONTROL = 224,
	KEY_LEFT_ALT = 226,
	KEY_LEFT_SUPER = 227,
	KEY_RIGHT_SHIFT = 229,
	KEY_RIGHT_CONTROL = 228,
	KEY_RIGHT_ALT = 230,
	KEY_RIGHT_SUPER = 231,
	KEY_MENU = 118
};
#endif
#ifdef OCULUS
enum class EKey {
	KEY_UNKNOWN = 0,
	KEY_SPACE = 44,
	KEY_APOSTROPHE = 49,
	KEY_COMMA = 54,
	KEY_MINUS = 45,
	KEY_PERIOD = 55,
	KEY_SLASH = 56,
	KEY_0 = 30,
	KEY_1 = 31,
	KEY_2 = 32,
	KEY_3 = 33,
	KEY_4 = 34,
	KEY_5 = 35,
	KEY_6 = 36,
	KEY_7 = 37,
	KEY_8 = 38,
	KEY_9 = 39,
	KEY_SEMICOLON = 51,
	KEY_EQUAL = 46,
	KEY_A = 4,
	KEY_B = 5,
	KEY_C = 6,
	KEY_D = 7,
	KEY_E = 8,
	KEY_F = 9,
	KEY_G = 10,
	KEY_H = 11,
	KEY_I = 12,
	KEY_J = 13,
	KEY_K = 14,
	KEY_L = 15,
	KEY_M = 16,
	KEY_N = 17,
	KEY_O = 18,
	KEY_P = 19,
	KEY_Q = 20,
	KEY_R = 21,
	KEY_S = 22,
	KEY_T = 23,
	KEY_U = 24,
	KEY_V = 25,
	KEY_W = 26,
	KEY_X = 27,
	KEY_Y = 28,
	KEY_Z = 29,
	KEY_LEFT_BRACKET = 47,
	KEY_BACKSLASH = 49,
	KEY_RIGHT_BRACKET = 48,
	KEY_GRAVE_ACCENT = 53,
	KEY_ESCAPE = 41,
	KEY_ENTER = 40,
	KEY_TAB = 43,
	KEY_BACKSPACE = 42,
	KEY_INSERT = 73,
	KEY_DELETE = 76,
	KEY_RIGHT = 79,
	KEY_LEFT = 80,
	KEY_DOWN = 81,
	KEY_UP = 82,
	KEY_PAGE_UP = 75,
	KEY_PAGE_DOWN = 78,
	KEY_HOME = 74,
	KEY_END = 77,
	KEY_CAPS_LOCK = 57,
	KEY_SCROLL_LOCK = 71,
	KEY_NUM_LOCK = 83,
	KEY_PRINT_SCREEN = 70,
	KEY_PAUSE = 72,
	KEY_F1 = 58,
	KEY_F2 = 59,
	KEY_F3 = 60,
	KEY_F4 = 61,
	KEY_F5 = 62,
	KEY_F6 = 63,
	KEY_F7 = 64,
	KEY_F8 = 65,
	KEY_F9 = 66,
	KEY_F10 = 67,
	KEY_F11 = 68,
	KEY_F12 = 69,
	KEY_F13 = 104,
	KEY_F14 = 105,
	KEY_F15 = 106,
	KEY_F16 = 107,
	KEY_F17 = 108,
	KEY_F18 = 109,
	KEY_F19 = 110,
	KEY_F20 = 111,
	KEY_F21 = 112,
	KEY_F22 = 113,
	KEY_F23 = 114,
	KEY_F24 = 115,
	KEY_KP_0 = 98,
	KEY_KP_1 = 89,
	KEY_KP_2 = 90,
	KEY_KP_3 = 91,
	KEY_KP_4 = 92,
	KEY_KP_5 = 93,
	KEY_KP_6 = 94,
	KEY_KP_7 = 95,
	KEY_KP_8 = 96,
	KEY_KP_9 = 97,
	KEY_KP_DECIMAL = 220,
	KEY_KP_DIVIDE = 84,
	KEY_KP_MULTIPLY = 85,
	KEY_KP_SUBTRACT = 86,
	KEY_KP_ADD = 87,
	KEY_KP_ENTER = 88,
	KEY_KP_EQUAL = 103,
	KEY_LEFT_SHIFT = 225,
	KEY_LEFT_CONTROL = 224,
	KEY_LEFT_ALT = 226,
	KEY_LEFT_SUPER = 227,
	KEY_RIGHT_SHIFT = 229,
	KEY_RIGHT_CONTROL = 228,
	KEY_RIGHT_ALT = 230,
	KEY_RIGHT_SUPER = 231,
	KEY_MENU = 118
};
#endif
enum class EKeyState { KEY_UP = 0, KEY_DOWN = 1 };

#ifdef USE_SDL
enum class EMouseButton {
	MOUSE_BUTTON_1 = 4,
	MOUSE_BUTTON_2 = 5,
	MOUSE_BUTTON_3 = 6,
	MOUSE_BUTTON_4 = 7,
	MOUSE_BUTTON_5 = 8,
	MOUSE_BUTTON_6 = 9,
	MOUSE_BUTTON_7 = 10,
	MOUSE_BUTTON_8 = 11,
	MOUSE_BUTTON_LEFT = 1,
	MOUSE_BUTTON_RIGHT = 3,
	MOUSE_BUTTON_MIDDLE = 2
};
#endif
#ifdef OCULUS
enum class EMouseButton {
	MOUSE_BUTTON_1 = 0,
	MOUSE_BUTTON_2 = 1,
	MOUSE_BUTTON_3 = 2,
	MOUSE_BUTTON_4 = 3,
	MOUSE_BUTTON_5 = 4,
	MOUSE_BUTTON_6 = 5,
	MOUSE_BUTTON_7 = 6,
	MOUSE_BUTTON_8 = 7,
	MOUSE_BUTTON_LEFT = 0,
	MOUSE_BUTTON_RIGHT = 1,
	MOUSE_BUTTON_MIDDLE = 2
};
#endif

enum class EMouseButtonState { MOUSE_UP = 0, MOUSE_DOWN = 1 };

inline constexpr std::int64_t kMousePointerId = 0;

class InputManager {
public:
	InputManager(WINDOW::Window& p_window);
	~InputManager();

	void beginInputFrame(std::chrono::duration<double> dt);

	[[nodiscard]] bool isKeyPressed(EKey p_key) const;
	[[nodiscard]] bool isKeyReleased(EKey p_key) const;
	[[nodiscard]] bool wasKeyPressedThisFrame(EKey p_key) const;
	[[nodiscard]] bool wasKeyReleasedThisFrame(EKey p_key) const;
	[[nodiscard]] bool isMouseButtonPressed(EMouseButton p_button) const;
	[[nodiscard]] bool isMouseButtonReleased(EMouseButton p_button) const;
	[[nodiscard]] bool wasMouseButtonPressedThisFrame(EMouseButton p_button) const;
	[[nodiscard]] bool wasMouseButtonReleasedThisFrame(EMouseButton p_button) const;
	[[nodiscard]] bool isButtonPressed(int id, INPUT::Gamepad::GAMEPAD_BUTTON btn) const;
	[[nodiscard]] bool wasButtonPressedThisFrame(int id, INPUT::Gamepad::GAMEPAD_BUTTON btn) const;
	[[nodiscard]] bool wasButtonReleasedThisFrame(int id, INPUT::Gamepad::GAMEPAD_BUTTON btn) const;
	[[nodiscard]] float getAxisPosition(int id, INPUT::Gamepad::GAMEPAD_AXIS axis) const;
	[[nodiscard]] float getTriggerValue(int id, INPUT::Gamepad::GAMEPAD_TRIGGER tgr) const;
	[[nodiscard]] bool isGamepadExist(int id) const;
	[[nodiscard]] MATH::Vector2i getMousePosition() const;
	[[nodiscard]] MATH::Vector2f getMouseDelta() const;
	[[nodiscard]] const INPUT::Gamepad* getFirstGamepad() const;
	void clearEvents();
	void endFrame();
	const INPUT::Gamepad* getGamepad(int id) const;

	[[nodiscard]] const std::vector<PointerEvent>& pointerEventsThisFrame() const {
		return mPointerEvents;
	}
	[[nodiscard]] const std::vector<SwipeEvent>& swipesThisFrame() const {
		return mSwipeRecognizer.swipesThisFrame();
	}
	[[nodiscard]] bool tryCapturePointer(std::int64_t pointerId);
	void releasePointerCapture(std::int64_t pointerId);
	[[nodiscard]] bool isPointerCaptured(std::int64_t pointerId) const;
	void configureVirtualStick(MATH::Vector2f center, float hitRadius, float deadzone);
	[[nodiscard]] MATH::Vector2f getVirtualStickValue() const;
	[[nodiscard]] DragCapture& dragCapture() {
		return mDragCapture;
	}
	[[nodiscard]] VirtualStick& virtualStick() {
		return mVirtualStick;
	}

private:
	void pushPointerEvent(PointerEvent event);
#ifdef USE_SDL
	void onTouchFinger(const WINDOW::TouchFingerEvent& event);
#endif
	void onKeyPressed(int val);
	void onKeyReleased(int val);
	void onMouseButtonPressed(int val);
	void onMouseButtonReleased(int val);
	void onMouseMoved(float dx, float dy);
	void onGamepadButtonPressed(int id, INPUT::Gamepad::GAMEPAD_BUTTON key);
	void onGamepadButtonReleased(int id, INPUT::Gamepad::GAMEPAD_BUTTON key);
	void onGamepadAxis(int id, INPUT::Gamepad::GAMEPAD_AXIS key, float val);
	void onGamepadTrigger(int id, INPUT::Gamepad::GAMEPAD_TRIGGER key, float val);

	void addGamepad(INPUT::Gamepad gp);
	void removeGamepad(int id);
	INPUT::Gamepad* getGamepad(int id);

	WINDOW::Window& mWindow;

	EVENT::Event<>::ID mKeyPressedListener = EVENT::Event<>::ID(0);
	EVENT::Event<>::ID mKeyReleasedListener = EVENT::Event<>::ID(0);
	EVENT::Event<>::ID mMouseButtonPressedListener = EVENT::Event<>::ID(0);
	EVENT::Event<>::ID mMouseButtonReleasedListener = EVENT::Event<>::ID(0);
	EVENT::Event<>::ID mMouseMovedListener = EVENT::Event<>::ID(0);

	EVENT::Event<>::ID mGamepadButtonPressedListener = EVENT::Event<>::ID(0);
	EVENT::Event<>::ID mGamepadButtonReleasedListener = EVENT::Event<>::ID(0);
	EVENT::Event<>::ID mGamepadAxisListener = EVENT::Event<>::ID(0);
	EVENT::Event<>::ID mGamepadTriggerListener = EVENT::Event<>::ID(0);

	EVENT::Event<>::ID mGamepadAddListener = EVENT::Event<>::ID(0);
	EVENT::Event<>::ID mGamepadRemoveListener = EVENT::Event<>::ID(0);
	EVENT::Event<>::ID mTouchFingerListener = EVENT::Event<>::ID(0);

	std::unordered_map<EKey, EKeyState> mKeyEvents;
	std::unordered_map<EMouseButton, EMouseButtonState> mMouseButtonEvents;
	std::unordered_set<EKey> mKeysPressedThisFrame;
	std::unordered_set<EKey> mKeysReleasedThisFrame;
	std::unordered_set<EMouseButton> mMousePressedThisFrame;
	std::unordered_set<EMouseButton> mMouseReleasedThisFrame;
	std::unordered_set<uint64_t> mGamepadPressedThisFrame;
	std::unordered_set<uint64_t> mGamepadReleasedThisFrame;
	MATH::Vector2f mMouseDelta;
	std::vector<INPUT::Gamepad> mGamepads;
	std::vector<PointerEvent> mPointerEvents;
	SwipeRecognizer mSwipeRecognizer;
	DragCapture mDragCapture;
	VirtualStick mVirtualStick;
	float mFrameDeltaSeconds = 0.0f;
	bool mMousePointerDown = false;
};
}
