#pragma once

#include "mathModule/math.h"

#include <cstdint>

namespace IKIGAI::GUI {

enum class EventType { HoverIn, HoverOut, Press, PressMove, PressFinish, PressCancel, FocusIn, FocusOut, Activate, ValueChanged };

struct Event {
	EventType type = EventType::HoverIn;
	// Pointer coordinates in viewport pixels and in the target's local space.
	MATH::Vector2f position {};
	MATH::Vector2f localPosition {};
	// Zero is the mouse. Keyboard, gamepad and other non-pointer events use -1.
	std::int64_t pointerId = -1;
};

} // namespace IKIGAI::GUI
