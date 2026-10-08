#include "gamepad.h"
#include <cmath>

using namespace IKIGAI::INPUT;

Gamepad::Gamepad(int number, std::string name) : mGamepadNumber(number), mName(std::move(name)) {
}

bool Gamepad::GamepadData::operator!=(const GamepadData& data) const {
	auto comparef = [](float x, float y, float epsilon = 0.00001f) {
		if (fabs(x - y) < epsilon)
			return true;
		return false;
	};
	if (!comparef(data.mRightTrigger, mRightTrigger) ||
		!comparef(data.mLeftTrigger, mLeftTrigger) ||
		!comparef(data.mRightSticX, mRightSticX) ||
		!comparef(data.mRightSticY, mRightSticY) ||
		!comparef(data.mLeftSticX, mLeftSticX) ||
		!comparef(data.mLeftSticY, mLeftSticY)) {
		return true;
	}
	return mButtons == data.mButtons;
}

bool Gamepad::isButtonPressed(GAMEPAD_BUTTON btn) const {
	return mData.mButtons.at(btn);
}

float Gamepad::getAxisPosition(GAMEPAD_AXIS axis) const {
	switch (axis) {
	case INPUT::Gamepad::GAMEPAD_AXIS::leftStick_X: return getData().mLeftSticX;
	case INPUT::Gamepad::GAMEPAD_AXIS::leftStick_Y: return getData().mLeftSticY;
	case INPUT::Gamepad::GAMEPAD_AXIS::rightStick_X: return getData().mRightSticX;
	case INPUT::Gamepad::GAMEPAD_AXIS::rightStick_Y: return getData().mRightSticY;
	}
	return 0.0f;
}

float Gamepad::getTriggerValue(GAMEPAD_TRIGGER tgr) const {
	switch (tgr) {
	case INPUT::Gamepad::GAMEPAD_TRIGGER::leftTrigger: return getData().mLeftTrigger;
	case INPUT::Gamepad::GAMEPAD_TRIGGER::rightTrigger: return getData().mRightTrigger;
	}
	return 0.0f;
}

const Gamepad::GamepadData& Gamepad::getData() const {
	return mData;
}

Gamepad::GamepadData& Gamepad::getData() {
	return mData;
}

int Gamepad::getId() const {
	return mGamepadNumber;
}



