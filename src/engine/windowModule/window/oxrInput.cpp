#ifdef OCULUS
#include "oxrInput.h"

#include "util_oxr.h"
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <algorithm>
#include <cmath>

namespace IKIGAI::WINDOW {
	namespace {
		constexpr float kAxisEpsilon = 0.01f;
		constexpr float kGripThreshold = 0.7f;

		bool NearlyEqual(float a, float b) {
			return std::fabs(a - b) < kAxisEpsilon;
		}
	}

	OxrInput::OxrInput(Window& window, XrInstance instance, XrSession session)
		: mWindow(window), mSession(session) {
		mActionSet = oxr_create_actionset(instance, "gameplay", "Gameplay", 0);
		mLeftPath = oxr_str2path(instance, "/user/hand/left");
		mRightPath = oxr_str2path(instance, "/user/hand/right");
		XrPath hands[2] = {mLeftPath, mRightPath};

		mButtonA = oxr_create_action(mActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "a", "A", 2, hands);
		mButtonB = oxr_create_action(mActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "b", "B", 2, hands);
		mButtonX = oxr_create_action(mActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "x", "X", 2, hands);
		mButtonY = oxr_create_action(mActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "y", "Y", 2, hands);
		mMenu = oxr_create_action(mActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu", 2, hands);
		mThumbClick = oxr_create_action(mActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumb_click", "Thumb click", 2, hands);
		mTrigger = oxr_create_action(mActionSet, XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger", 2, hands);
		mSqueeze = oxr_create_action(mActionSet, XR_ACTION_TYPE_FLOAT_INPUT, "squeeze", "Squeeze", 2, hands);
		mThumbstick = oxr_create_action(mActionSet, XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick", 2, hands);

		std::vector<XrActionSuggestedBinding> bindings;
		auto bind = [&](XrAction action, const char* path) {
			bindings.push_back({action, oxr_str2path(instance, path)});
		};
		bind(mButtonX, "/user/hand/left/input/x/click");
		bind(mButtonY, "/user/hand/left/input/y/click");
		bind(mMenu, "/user/hand/left/input/menu/click");
		bind(mThumbClick, "/user/hand/left/input/thumbstick/click");
		bind(mTrigger, "/user/hand/left/input/trigger/value");
		bind(mSqueeze, "/user/hand/left/input/squeeze/value");
		bind(mThumbstick, "/user/hand/left/input/thumbstick");
		bind(mButtonA, "/user/hand/right/input/a/click");
		bind(mButtonB, "/user/hand/right/input/b/click");
		bind(mThumbClick, "/user/hand/right/input/thumbstick/click");
		bind(mTrigger, "/user/hand/right/input/trigger/value");
		bind(mSqueeze, "/user/hand/right/input/squeeze/value");
		bind(mThumbstick, "/user/hand/right/input/thumbstick");
		oxr_bind_interaction(instance, "/interaction_profiles/oculus/touch_controller", bindings);
		oxr_bind_interaction(instance, "/interaction_profiles/facebook/touch_controller", bindings);
		oxr_bind_interaction(instance, "/interaction_profiles/meta/touch_controller_pro", bindings);
		oxr_attach_actionsets(session, mActionSet);
	}

	void OxrInput::ensureAnnounced() {
		if (mAnnounced) {
			return;
		}
		mAnnounced = true;
		mWindow.gamepadAddEvent.run(INPUT::Gamepad(0, "Quest Touch"));
		mWindow.gamepadAddEvent.run(INPUT::Gamepad(1, "Quest Touch Left"));
		mWindow.gamepadAddEvent.run(INPUT::Gamepad(2, "Quest Touch Right"));
	}

	int32_t OxrInput::handleAndroidInput(AInputEvent* event) {
		ensureAnnounced();
		using Btn = INPUT::Gamepad::GAMEPAD_BUTTON;
		using Axis = INPUT::Gamepad::GAMEPAD_AXIS;
		using Trig = INPUT::Gamepad::GAMEPAD_TRIGGER;

		const int32_t type = AInputEvent_getType(event);
		if (type == AINPUT_EVENT_TYPE_KEY) {
			const int32_t action = AKeyEvent_getAction(event);
			const int32_t code = AKeyEvent_getKeyCode(event);
			__android_log_print(ANDROID_LOG_INFO, "IKIGAI", "Quest android KEY code=%d action=%d", code, action);
			if (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_UP) {
				return 1;
			}
			if (action == AKEY_EVENT_ACTION_DOWN && AKeyEvent_getRepeatCount(event) > 0) {
				return 1;
			}
			const bool down = action == AKEY_EVENT_ACTION_DOWN;
			switch (code) {
			case AKEYCODE_BUTTON_A:
			case AKEYCODE_A:
				emitButton(0, Btn::btn_a, down, mCombined.a);
				emitButton(2, Btn::btn_a, down, mRightHand.a);
				return 1;
			case AKEYCODE_BUTTON_B:
			case AKEYCODE_B:
				emitButton(0, Btn::btn_b, down, mCombined.b);
				emitButton(2, Btn::btn_b, down, mRightHand.b);
				return 1;
			case AKEYCODE_BUTTON_X:
			case AKEYCODE_X:
				emitButton(0, Btn::btn_x, down, mCombined.x);
				emitButton(1, Btn::btn_x, down, mLeft.x);
				return 1;
			case AKEYCODE_BUTTON_Y:
			case AKEYCODE_Y:
				emitButton(0, Btn::btn_y, down, mCombined.y);
				emitButton(1, Btn::btn_y, down, mLeft.y);
				return 1;
			case AKEYCODE_BUTTON_L1:
				emitButton(0, Btn::btn_lb, down, mCombined.grip);
				emitButton(1, Btn::btn_lb, down, mLeft.grip);
				return 1;
			case AKEYCODE_BUTTON_R1:
				emitButton(0, Btn::btn_rb, down, mRight.grip);
				emitButton(2, Btn::btn_lb, down, mRightHand.grip);
				return 1;
			case AKEYCODE_BUTTON_L2:
				mAndroidLeftTrigger = down;
				emitTrigger(0, Trig::leftTrigger, down ? 1.0f : 0.0f, mCombined.trigger);
				emitTrigger(1, Trig::leftTrigger, down ? 1.0f : 0.0f, mLeft.trigger);
				return 1;
			case AKEYCODE_BUTTON_R2:
				mAndroidRightTrigger = down;
				emitTrigger(0, Trig::rightTrigger, down ? 1.0f : 0.0f, mRight.trigger);
				emitTrigger(2, Trig::leftTrigger, down ? 1.0f : 0.0f, mRightHand.trigger);
				return 1;
			case AKEYCODE_BUTTON_THUMBL:
				emitButton(0, Btn::btn_leftStick, down, mCombined.thumb);
				emitButton(1, Btn::btn_leftStick, down, mLeft.thumb);
				return 1;
			case AKEYCODE_BUTTON_THUMBR:
				emitButton(0, Btn::btn_rightStick, down, mRight.thumb);
				emitButton(2, Btn::btn_leftStick, down, mRightHand.thumb);
				return 1;
			case AKEYCODE_BUTTON_START:
			case AKEYCODE_MENU:
				emitButton(0, Btn::btn_start, down, mCombined.menu);
				emitButton(1, Btn::btn_start, down, mLeft.menu);
				return 1;
			default:
				return 0;
			}
		}

		if (type == AINPUT_EVENT_TYPE_MOTION) {
			emitAxis(0, Axis::leftStick_X, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0), mCombined.stickX);
			emitAxis(0, Axis::leftStick_Y, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0), mCombined.stickY);
			emitAxis(0, Axis::rightStick_X, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0), mRight.stickX);
			emitAxis(0, Axis::rightStick_Y, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0), mRight.stickY);
			const float lTrig = std::max({
				AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0),
				AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_BRAKE, 0),
				mAndroidLeftTrigger ? 1.0f : 0.0f
			});
			const float rTrig = std::max({
				AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0),
				AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_GAS, 0),
				mAndroidRightTrigger ? 1.0f : 0.0f
			});
			emitTrigger(0, Trig::leftTrigger, lTrig, mCombined.trigger);
			emitTrigger(1, Trig::leftTrigger, lTrig, mLeft.trigger);
			emitTrigger(0, Trig::rightTrigger, rTrig, mRight.trigger);
			emitTrigger(2, Trig::leftTrigger, rTrig, mRightHand.trigger);
			return 1;
		}
		return 0;
	}

	void OxrInput::emitButton(int padId, INPUT::Gamepad::GAMEPAD_BUTTON button, bool down, bool& prev) {
		if (down == prev) {
			return;
		}
		prev = down;
		if (down) {
			mWindow.gamepadButtonPressedEvent.run(padId, button);
		} else {
			mWindow.gamepadButtonReleasedEvent.run(padId, button);
		}
	}

	void OxrInput::emitAxis(int padId, INPUT::Gamepad::GAMEPAD_AXIS axis, float value, float& prev) {
		if (NearlyEqual(value, prev)) {
			return;
		}
		prev = value;
		mWindow.gamepadAxisEvent.run(padId, axis, value);
	}

	void OxrInput::emitTrigger(int padId, INPUT::Gamepad::GAMEPAD_TRIGGER trigger, float value, float& prev) {
		if (NearlyEqual(value, prev)) {
			return;
		}
		prev = value;
		mWindow.gamepadTriggerEvent.run(padId, trigger, value);
	}

	void OxrInput::sync() {
		ensureAnnounced();
		if (!oxr_is_session_running()) {
			return;
		}
		oxr_sync_actions(mSession, mActionSet);

		const auto aState = oxr_get_action_state_boolean(mSession, mButtonA, mRightPath);
		const auto bState = oxr_get_action_state_boolean(mSession, mButtonB, mRightPath);
		const auto xState = oxr_get_action_state_boolean(mSession, mButtonX, mLeftPath);
		const auto yState = oxr_get_action_state_boolean(mSession, mButtonY, mLeftPath);
		const auto menuState = oxr_get_action_state_boolean(mSession, mMenu, mLeftPath);
		const auto lThumbState = oxr_get_action_state_boolean(mSession, mThumbClick, mLeftPath);
		const auto rThumbState = oxr_get_action_state_boolean(mSession, mThumbClick, mRightPath);
		const auto lTrigState = oxr_get_action_state_float(mSession, mTrigger, mLeftPath);
		const auto rTrigState = oxr_get_action_state_float(mSession, mTrigger, mRightPath);
		const auto lGripState = oxr_get_action_state_float(mSession, mSqueeze, mLeftPath);
		const auto rGripState = oxr_get_action_state_float(mSession, mSqueeze, mRightPath);
		const auto leftStick = oxr_get_action_state_vector2(mSession, mThumbstick, mLeftPath);
		const auto rightStick = oxr_get_action_state_vector2(mSession, mThumbstick, mRightPath);

		using Btn = INPUT::Gamepad::GAMEPAD_BUTTON;
		using Axis = INPUT::Gamepad::GAMEPAD_AXIS;
		using Trig = INPUT::Gamepad::GAMEPAD_TRIGGER;

		if (aState.isActive) {
			emitButton(0, Btn::btn_a, aState.currentState, mCombined.a);
			emitButton(2, Btn::btn_a, aState.currentState, mRightHand.a);
		}
		if (bState.isActive) {
			emitButton(0, Btn::btn_b, bState.currentState, mCombined.b);
			emitButton(2, Btn::btn_b, bState.currentState, mRightHand.b);
		}
		if (xState.isActive) {
			emitButton(0, Btn::btn_x, xState.currentState, mCombined.x);
			emitButton(1, Btn::btn_x, xState.currentState, mLeft.x);
		}
		if (yState.isActive) {
			emitButton(0, Btn::btn_y, yState.currentState, mCombined.y);
			emitButton(1, Btn::btn_y, yState.currentState, mLeft.y);
		}
		if (menuState.isActive) {
			emitButton(0, Btn::btn_start, menuState.currentState, mCombined.menu);
			emitButton(1, Btn::btn_start, menuState.currentState, mLeft.menu);
		}
		if (lThumbState.isActive) {
			emitButton(0, Btn::btn_leftStick, lThumbState.currentState, mCombined.thumb);
			emitButton(1, Btn::btn_leftStick, lThumbState.currentState, mLeft.thumb);
		}
		if (rThumbState.isActive) {
			emitButton(0, Btn::btn_rightStick, rThumbState.currentState, mRight.thumb);
			emitButton(2, Btn::btn_leftStick, rThumbState.currentState, mRightHand.thumb);
		}
		if (lGripState.isActive) {
			const bool lGrip = lGripState.currentState >= kGripThreshold;
			emitButton(0, Btn::btn_lb, lGrip, mCombined.grip);
			emitButton(1, Btn::btn_lb, lGrip, mLeft.grip);
		}
		if (rGripState.isActive) {
			const bool rGrip = rGripState.currentState >= kGripThreshold;
			emitButton(0, Btn::btn_rb, rGrip, mRight.grip);
			emitButton(2, Btn::btn_lb, rGrip, mRightHand.grip);
		}
		if (lTrigState.isActive && !mAndroidLeftTrigger) {
			emitTrigger(0, Trig::leftTrigger, lTrigState.currentState, mCombined.trigger);
			emitTrigger(1, Trig::leftTrigger, lTrigState.currentState, mLeft.trigger);
		}
		if (rTrigState.isActive && !mAndroidRightTrigger) {
			emitTrigger(0, Trig::rightTrigger, rTrigState.currentState, mRight.trigger);
			emitTrigger(2, Trig::leftTrigger, rTrigState.currentState, mRightHand.trigger);
		}
		if (leftStick.isActive) {
			emitAxis(0, Axis::leftStick_X, leftStick.currentState.x, mCombined.stickX);
			emitAxis(0, Axis::leftStick_Y, leftStick.currentState.y, mCombined.stickY);
			emitAxis(1, Axis::leftStick_X, leftStick.currentState.x, mLeft.stickX);
			emitAxis(1, Axis::leftStick_Y, leftStick.currentState.y, mLeft.stickY);
		}
		if (rightStick.isActive) {
			emitAxis(0, Axis::rightStick_X, rightStick.currentState.x, mRight.stickX);
			emitAxis(0, Axis::rightStick_Y, rightStick.currentState.y, mRight.stickY);
			emitAxis(2, Axis::leftStick_X, rightStick.currentState.x, mRightHand.stickX);
			emitAxis(2, Axis::leftStick_Y, rightStick.currentState.y, mRightHand.stickY);
		}
	}

}
#endif
