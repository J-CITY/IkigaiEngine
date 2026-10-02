#pragma once

#include "../window/window.h"
#include <unordered_set>
#include <utilsModule/event.h>

namespace IKIGAI::INPUT_SYSTEM {
//#if defined USE_GLFW || defined DX12_BACKEND
//	enum class EKey {
//		KEY_UNKNOWN = -1,
//		KEY_SPACE = 32,
//		KEY_APOSTROPHE = 39,
//		KEY_COMMA = 44,
//		KEY_MINUS = 45,
//		KEY_PERIOD = 46,
//		KEY_SLASH = 47,
//		KEY_0 = 48,
//		KEY_1 = 49,
//		KEY_2 = 50,
//		KEY_3 = 51,
//		KEY_4 = 52,
//		KEY_5 = 53,
//		KEY_6 = 54,
//		KEY_7 = 55,
//		KEY_8 = 56,
//		KEY_9 = 57,
//		KEY_SEMICOLON = 59,
//		KEY_EQUAL = 61,
//		KEY_A = 65,
//		KEY_B = 66,
//		KEY_C = 67,
//		KEY_D = 68,
//		KEY_E = 69,
//		KEY_F = 70,
//		KEY_G = 71,
//		KEY_H = 72,
//		KEY_I = 73,
//		KEY_J = 74,
//		KEY_K = 75,
//		KEY_L = 76,
//		KEY_M = 77,
//		KEY_N = 78,
//		KEY_O = 79,
//		KEY_P = 80,
//		KEY_Q = 81,
//		KEY_R = 82,
//		KEY_S = 83,
//		KEY_T = 84,
//		KEY_U = 85,
//		KEY_V = 86,
//		KEY_W = 87,
//		KEY_X = 88,
//		KEY_Y = 89,
//		KEY_Z = 90,
//		KEY_LEFT_BRACKET = 91,
//		KEY_BACKSLASH = 92,
//		KEY_RIGHT_BRACKET = 93,
//		KEY_GRAVE_ACCENT = 96,
//		KEY_WORLD_1 = 61,
//		KEY_WORLD_2 = 62,
//		KEY_ESCAPE = 256,
//		KEY_ENTER = 257,
//		KEY_TAB = 258,
//		KEY_BACKSPACE = 259,
//		KEY_INSERT = 260,
//		KEY_DELETE = 261,
//		KEY_RIGHT = 262,
//		KEY_LEFT = 263,
//		KEY_DOWN = 264,
//		KEY_UP = 265,
//		KEY_PAGE_UP = 266,
//		KEY_PAGE_DOWN = 267,
//		KEY_HOME = 268,
//		KEY_END = 269,
//		KEY_CAPS_LOCK = 280,
//		KEY_SCROLL_LOCK = 281,
//		KEY_NUM_LOCK = 282,
//		KEY_PRINT_SCREEN = 283,
//		KEY_PAUSE = 284,
//		KEY_F1 = 290,
//		KEY_F2 = 291,
//		KEY_F3 = 292,
//		KEY_F4 = 293,
//		KEY_F5 = 294,
//		KEY_F6 = 295,
//		KEY_F7 = 296,
//		KEY_F8 = 297,
//		KEY_F9 = 298,
//		KEY_F10 = 299,
//		KEY_F11 = 300,
//		KEY_F12 = 301,
//		KEY_F13 = 302,
//		KEY_F14 = 303,
//		KEY_F15 = 304,
//		KEY_F16 = 305,
//		KEY_F17 = 306,
//		KEY_F18 = 307,
//		KEY_F19 = 308,
//		KEY_F20 = 309,
//		KEY_F21 = 310,
//		KEY_F22 = 311,
//		KEY_F23 = 312,
//		KEY_F24 = 313,
//		KEY_F25 = 314,
//		KEY_KP_0 = 320,
//		KEY_KP_1 = 321,
//		KEY_KP_2 = 322,
//		KEY_KP_3 = 323,
//		KEY_KP_4 = 324,
//		KEY_KP_5 = 325,
//		KEY_KP_6 = 326,
//		KEY_KP_7 = 327,
//		KEY_KP_8 = 328,
//		KEY_KP_9 = 329,
//		KEY_KP_DECIMAL = 330,
//		KEY_KP_DIVIDE = 331,
//		KEY_KP_MULTIPLY = 332,
//		KEY_KP_SUBTRACT = 333,
//		KEY_KP_ADD = 334,
//		KEY_KP_ENTER = 335,
//		KEY_KP_EQUAL = 336,
//		KEY_LEFT_SHIFT = 340,
//		KEY_LEFT_CONTROL = 341,
//		KEY_LEFT_ALT = 342,
//		KEY_LEFT_SUPER = 343,
//		KEY_RIGHT_SHIFT = 344,
//		KEY_RIGHT_CONTROL = 345,
//		KEY_RIGHT_ALT = 346,
//		KEY_RIGHT_SUPER = 347,
//		KEY_MENU = 348
//	};
//#endif
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
		//KEY_WORLD_1 = 61,//?
		//KEY_WORLD_2 = 62,//?
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
		//KEY_F25 = 314,
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
	};
#endif
	enum class EKeyState {
		KEY_UP = 0,
		KEY_DOWN = 1
	};

#ifdef USE_GLFW
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

//#ifdef DX12_BACKEND
//	enum class EMouseButton {
//		MOUSE_BUTTON_1 = 0,
//		MOUSE_BUTTON_2 = 1,
//		MOUSE_BUTTON_3 = 2,
//		MOUSE_BUTTON_4 = 3,
//		MOUSE_BUTTON_5 = 4,
//		MOUSE_BUTTON_6 = 5,
//		MOUSE_BUTTON_7 = 6,
//		MOUSE_BUTTON_8 = 7,
//		MOUSE_BUTTON_LEFT = 0,
//		MOUSE_BUTTON_RIGHT = 1,
//		MOUSE_BUTTON_MIDDLE = 2
//	};
//#endif

	enum class EMouseButtonState {
		MOUSE_UP = 0,
		MOUSE_DOWN = 1
	};
	class InputManager {
	public:
		InputManager(WINDOW::Window& p_window);
		~InputManager();

		[[nodiscard]] bool isKeyPressed(EKey p_key) const;
		[[nodiscard]] bool isKeyReleased(EKey p_key) const;
		[[nodiscard]] bool wasKeyPressedThisFrame(EKey p_key) const;
		[[nodiscard]] bool wasKeyReleasedThisFrame(EKey p_key) const;
		[[nodiscard]] bool isMouseButtonPressed(EMouseButton p_button) const;
		[[nodiscard]] bool isMouseButtonReleased(EMouseButton p_button) const ;
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
	private:
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

		
		std::unordered_map<EKey, EKeyState> mKeyEvents;
		std::unordered_map<EMouseButton, EMouseButtonState>	mMouseButtonEvents;
		std::unordered_set<EKey> mKeysPressedThisFrame;
		std::unordered_set<EKey> mKeysReleasedThisFrame;
		std::unordered_set<EMouseButton> mMousePressedThisFrame;
		std::unordered_set<EMouseButton> mMouseReleasedThisFrame;
		std::unordered_set<uint64_t> mGamepadPressedThisFrame;
		std::unordered_set<uint64_t> mGamepadReleasedThisFrame;
		MATH::Vector2f mMouseDelta;
		std::vector<INPUT::Gamepad> mGamepads;
	};
}
