#pragma once

#ifdef OCULUS
#include "window.h"
#include <cstdint>

namespace IKIGAI::WINDOW {

	class OxrInput {
	public:
		OxrInput(Window& window, XrInstance instance, XrSession session);
		void sync();
		int32_t handleAndroidInput(AInputEvent* event);

	private:
		void ensureAnnounced();
		void emitButton(int padId, INPUT::Gamepad::GAMEPAD_BUTTON button, bool down, bool& prev);
		void emitAxis(int padId, INPUT::Gamepad::GAMEPAD_AXIS axis, float value, float& prev);
		void emitTrigger(int padId, INPUT::Gamepad::GAMEPAD_TRIGGER trigger, float value, float& prev);

		Window& mWindow;
		XrSession mSession = XR_NULL_HANDLE;
		XrActionSet mActionSet = XR_NULL_HANDLE;
		XrPath mLeftPath = XR_NULL_PATH;
		XrPath mRightPath = XR_NULL_PATH;

		XrAction mButtonA = XR_NULL_HANDLE;
		XrAction mButtonB = XR_NULL_HANDLE;
		XrAction mButtonX = XR_NULL_HANDLE;
		XrAction mButtonY = XR_NULL_HANDLE;
		XrAction mMenu = XR_NULL_HANDLE;
		XrAction mThumbClick = XR_NULL_HANDLE;
		XrAction mTrigger = XR_NULL_HANDLE;
		XrAction mSqueeze = XR_NULL_HANDLE;
		XrAction mThumbstick = XR_NULL_HANDLE;

		struct PadState {
			bool a = false;
			bool b = false;
			bool x = false;
			bool y = false;
			bool menu = false;
			bool thumb = false;
			bool grip = false;
			float trigger = 0.0f;
			float stickX = 0.0f;
			float stickY = 0.0f;
		};
		PadState mLeft;
		PadState mRight;
		PadState mCombined;
		PadState mRightHand;
		bool mAnnounced = false;
		bool mAndroidLeftTrigger = false;
		bool mAndroidRightTrigger = false;
	};

}
#endif
