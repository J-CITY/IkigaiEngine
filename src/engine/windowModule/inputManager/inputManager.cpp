#include "inputManager.h"

#include <stdexcept>
#include "utilsModule/log/loggerDefine.h"

using namespace IKIGAI::INPUT_SYSTEM;

//TODO: remove USE_STL after add wrapper to oculus window

InputManager::InputManager(IKIGAI::WINDOW::Window& _window) : mWindow(_window) {

	mKeyPressedListener = mWindow.keyPressedEvent.add(std::bind(&InputManager::onKeyPressed, this, std::placeholders::_1));
	mKeyReleasedListener = mWindow.keyReleasedEvent.add(std::bind(&InputManager::onKeyReleased, this, std::placeholders::_1));
	mMouseButtonPressedListener = mWindow.mouseButtonPressedEvent.add(std::bind(&InputManager::onMouseButtonPressed, this, std::placeholders::_1));
	mMouseButtonReleasedListener = mWindow.mouseButtonReleasedEvent.add(std::bind(&InputManager::onMouseButtonReleased, this, std::placeholders::_1));
	mMouseMovedListener = mWindow.mouseMovedEvent.add(std::bind(&InputManager::onMouseMoved, this, std::placeholders::_1, std::placeholders::_2));

	mGamepadButtonPressedListener = mWindow.gamepadButtonPressedEvent.add(std::bind(&InputManager::onGamepadButtonPressed, this, std::placeholders::_1, std::placeholders::_2));
	mGamepadButtonReleasedListener = mWindow.gamepadButtonReleasedEvent.add(std::bind(&InputManager::onGamepadButtonReleased, this, std::placeholders::_1, std::placeholders::_2));
	mGamepadAxisListener = mWindow.gamepadAxisEvent.add(std::bind(&InputManager::onGamepadAxis, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	mGamepadTriggerListener = mWindow.gamepadTriggerEvent.add(std::bind(&InputManager::onGamepadTrigger, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	mGamepadAddListener = mWindow.gamepadAddEvent.add(std::bind(&InputManager::addGamepad, this, std::placeholders::_1));
	mGamepadRemoveListener = mWindow.gamepadRemoveEvent.add(std::bind(&InputManager::removeGamepad, this, std::placeholders::_1));
}

InputManager::~InputManager() {

	mWindow.keyPressedEvent.removeListener(mKeyPressedListener);
	mWindow.keyReleasedEvent.removeListener(mKeyReleasedListener);
	mWindow.mouseButtonPressedEvent.removeListener(mMouseButtonPressedListener);
	mWindow.mouseButtonReleasedEvent.removeListener(mMouseButtonReleasedListener);
	mWindow.mouseMovedEvent.removeListener(mMouseMovedListener);
	mWindow.gamepadButtonPressedEvent.removeListener(mGamepadButtonPressedListener);
	mWindow.gamepadButtonReleasedEvent.removeListener(mGamepadButtonReleasedListener);
	mWindow.gamepadAxisEvent.removeListener(mGamepadAxisListener);
	mWindow.gamepadTriggerEvent.removeListener(mGamepadTriggerListener);
	mWindow.gamepadAddEvent.removeListener(mGamepadAddListener);
	mWindow.gamepadRemoveEvent.removeListener(mGamepadRemoveListener);
}

bool InputManager::isKeyPressed(EKey p_key) const {
	return mKeyEvents.find(p_key) != mKeyEvents.end() && mKeyEvents.at(p_key) == EKeyState::KEY_DOWN;
}

bool InputManager::isKeyReleased(EKey p_key) const {
	return mKeyEvents.find(p_key) != mKeyEvents.end() && mKeyEvents.at(p_key) == EKeyState::KEY_UP;
}

bool InputManager::wasKeyPressedThisFrame(EKey p_key) const {
	return mKeysPressedThisFrame.contains(p_key);
}

bool InputManager::wasKeyReleasedThisFrame(EKey p_key) const {
	return mKeysReleasedThisFrame.contains(p_key);
}

bool InputManager::isMouseButtonPressed(EMouseButton p_button) const {
	return mMouseButtonEvents.find(p_button) != mMouseButtonEvents.end() && mMouseButtonEvents.at(p_button) == EMouseButtonState::MOUSE_DOWN;
}

bool InputManager::isMouseButtonReleased(EMouseButton p_button) const {
	bool res = mMouseButtonEvents.find(p_button) != mMouseButtonEvents.end() && mMouseButtonEvents.at(p_button) == EMouseButtonState::MOUSE_UP;
	return res;
}

bool InputManager::wasMouseButtonPressedThisFrame(EMouseButton p_button) const {
	return mMousePressedThisFrame.contains(p_button);
}

bool InputManager::wasMouseButtonReleasedThisFrame(EMouseButton p_button) const {
	return mMouseReleasedThisFrame.contains(p_button);
}

namespace {
	uint64_t GamepadButtonKey(int id, IKIGAI::INPUT::Gamepad::GAMEPAD_BUTTON btn) {
		return (static_cast<uint64_t>(static_cast<uint32_t>(id)) << 32) | static_cast<uint32_t>(btn);
	}
}

bool InputManager::isButtonPressed(int id, INPUT::Gamepad::GAMEPAD_BUTTON btn) const {
	const auto gp = getGamepad(id);
	if (!gp) {
		LOG_ERROR << "Wrong gamepad id: " << id;
		return false;
	}
	return gp->getData().mButtons.at(btn);
}

bool InputManager::wasButtonPressedThisFrame(int id, INPUT::Gamepad::GAMEPAD_BUTTON btn) const {
	return mGamepadPressedThisFrame.contains(GamepadButtonKey(id, btn));
}

bool InputManager::wasButtonReleasedThisFrame(int id, INPUT::Gamepad::GAMEPAD_BUTTON btn) const {
	return mGamepadReleasedThisFrame.contains(GamepadButtonKey(id, btn));
}

float InputManager::getAxisPosition(int id, INPUT::Gamepad::GAMEPAD_AXIS axis) const {
	const auto gp = getGamepad(id);
	if (!gp) {
		LOG_ERROR << "Wrong gamepad id: " << id;
		return 0.0f;
	}
	switch (axis) {
	case INPUT::Gamepad::GAMEPAD_AXIS::leftStick_X: return gp->getData().mLeftSticX;
	case INPUT::Gamepad::GAMEPAD_AXIS::leftStick_Y: return gp->getData().mLeftSticY;
	case INPUT::Gamepad::GAMEPAD_AXIS::rightStick_X: return gp->getData().mRightSticX;
	case INPUT::Gamepad::GAMEPAD_AXIS::rightStick_Y: return gp->getData().mRightSticY;
	}
	return 0.0f;
}

float InputManager::getTriggerValue(int id, INPUT::Gamepad::GAMEPAD_TRIGGER tgr) const {
	const auto gp = getGamepad(id);
	if (!gp) {
		LOG_ERROR << "Wrong gamepad id: " << id;
		return 0.0f;
	}
	switch (tgr) {
	case INPUT::Gamepad::GAMEPAD_TRIGGER::rightTrigger: return gp->getData().mLeftTrigger;
	case INPUT::Gamepad::GAMEPAD_TRIGGER::leftTrigger: return gp->getData().mRightTrigger;
	}
	return 0.0f;
}

bool InputManager::isGamepadExist(int id) const {
	const auto gp = getGamepad(id);
	return gp != nullptr;
}

IKIGAI::MATH::Vector2i InputManager::getMousePosition() const {
	return mWindow.getMousePos();
}

IKIGAI::MATH::Vector2f InputManager::getMouseDelta() const {
	return mMouseDelta;
}

const IKIGAI::INPUT::Gamepad* InputManager::getFirstGamepad() const {
	return mGamepads.empty() ? nullptr : &mGamepads.front();
}

void InputManager::clearEvents() {
	mKeyEvents.clear();
	mMouseButtonEvents.clear();
	endFrame();
}

void InputManager::endFrame() {
	mKeysPressedThisFrame.clear();
	mKeysReleasedThisFrame.clear();
	mMousePressedThisFrame.clear();
	mMouseReleasedThisFrame.clear();
	mGamepadPressedThisFrame.clear();
	mGamepadReleasedThisFrame.clear();
	mMouseDelta = {};
}

void InputManager::onKeyPressed(int p_key) {
	const auto key = static_cast<EKey>(p_key);
	mKeyEvents[key] = EKeyState::KEY_DOWN;
	mKeysPressedThisFrame.insert(key);
}

void InputManager::onKeyReleased(int p_key) {
	const auto key = static_cast<EKey>(p_key);
	mKeyEvents[key] = EKeyState::KEY_UP;
	mKeysReleasedThisFrame.insert(key);
}

void InputManager::onMouseButtonPressed(int p_button) {
	const auto button = static_cast<EMouseButton>(p_button);
	mMouseButtonEvents[button] = EMouseButtonState::MOUSE_DOWN;
	mMousePressedThisFrame.insert(button);
}

void InputManager::onMouseButtonReleased(int p_button) {
	const auto button = static_cast<EMouseButton>(p_button);
	mMouseButtonEvents[button] = EMouseButtonState::MOUSE_UP;
	mMouseReleasedThisFrame.insert(button);
}

void InputManager::onMouseMoved(float dx, float dy) {
	mMouseDelta.x += dx;
	mMouseDelta.y += dy;
}

void InputManager::onGamepadButtonPressed(int id, INPUT::Gamepad::GAMEPAD_BUTTON key) {
	auto gp = getGamepad(id);
	if (!gp) {
		return;
	}
	gp->getData().mButtons[key] = true;
	mGamepadPressedThisFrame.insert(GamepadButtonKey(id, key));
}

void InputManager::onGamepadButtonReleased(int id, INPUT::Gamepad::GAMEPAD_BUTTON key) {
	auto gp = getGamepad(id);
	if (!gp) {
		return;
	}
	gp->getData().mButtons[key] = false;
	mGamepadReleasedThisFrame.insert(GamepadButtonKey(id, key));
}

void InputManager::onGamepadAxis(int id, INPUT::Gamepad::GAMEPAD_AXIS key, float val) {
	auto gp = getGamepad(id);
	if (!gp) {
		return;
	}
	switch (key) {
	case INPUT::Gamepad::GAMEPAD_AXIS::leftStick_X: gp->getData().mLeftSticX = val; break;
	case INPUT::Gamepad::GAMEPAD_AXIS::leftStick_Y: gp->getData().mLeftSticY = val; break;
	case INPUT::Gamepad::GAMEPAD_AXIS::rightStick_X: gp->getData().mRightSticX = val; break;
	case INPUT::Gamepad::GAMEPAD_AXIS::rightStick_Y: gp->getData().mRightSticY = val; break;
	default: break;
	}
}

void InputManager::onGamepadTrigger(int id, INPUT::Gamepad::GAMEPAD_TRIGGER key, float val) {
	auto gp = getGamepad(id);
	if (!gp) {
		return;
	}
	switch (key) {
	case INPUT::Gamepad::GAMEPAD_TRIGGER::leftTrigger: gp->getData().mLeftTrigger = val; break;
	case INPUT::Gamepad::GAMEPAD_TRIGGER::rightTrigger: gp->getData().mRightTrigger = val; break;
	default: break;
	}
}

void InputManager::addGamepad(INPUT::Gamepad gp) {
	mGamepads.push_back(gp);
}

void InputManager::removeGamepad(int id) {
	std::erase_if(mGamepads, [id](const INPUT::Gamepad& e) {
		return e.getId() == id;
	});
}

const IKIGAI::INPUT::Gamepad* InputManager::getGamepad(int id) const {
	for (auto& e : mGamepads) {
		if (e.getId() == id) {
			return &e;
		}
	}
	return nullptr;
}

IKIGAI::INPUT::Gamepad* InputManager::getGamepad(int id) {
	for (auto& e : mGamepads) {
		if (e.getId() == id) {
			return &e;
		}
	}
	return nullptr;
}
