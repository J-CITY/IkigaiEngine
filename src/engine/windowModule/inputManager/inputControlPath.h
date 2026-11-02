#pragma once

#include "inputManager.h"
#include <mathModule/math.h>
#include <optional>
#include <string>
#include <vector>

namespace IKIGAI::INPUT_SYSTEM {
struct InputBindingDesc;
struct InputProcessorDesc;

struct ControlSample {
	bool down = false;
	bool pressedThisFrame = false;
	bool releasedThisFrame = false;
	float value = 0.0f;
	MATH::Vector2f value2;
	bool hasVec2 = false;
};

enum class ControlSourceType {
	None,
	KeyboardKey,
	MouseButton,
	MouseDelta,
	MousePosition,
	GamepadButton,
	GamepadLeftStick,
	GamepadRightStick,
	GamepadLeftTrigger,
	GamepadRightTrigger,
};

struct ResolvedControlPath {
	ControlSourceType type = ControlSourceType::None;
	EKey key = EKey::KEY_UNKNOWN;
	EMouseButton mouseButton = EMouseButton::MOUSE_BUTTON_LEFT;
	INPUT::Gamepad::GAMEPAD_BUTTON gamepadButton = INPUT::Gamepad::GAMEPAD_BUTTON::btn_a;
};

enum class BindingShape { Single, Vector2, Axis1 };

struct ResolvedBinding {
	BindingShape shape = BindingShape::Single;
	ResolvedControlPath path;
	ResolvedControlPath up;
	ResolvedControlPath down;
	ResolvedControlPath left;
	ResolvedControlPath right;
	ResolvedControlPath positive;
	ResolvedControlPath negative;
	std::string interaction = "Press";
	float duration = 0.4f;
	float maxDuration = 0.2f;
};

bool ResolveControlPath(const std::string& rawPath, ResolvedControlPath& out);
bool ResolveBinding(const InputBindingDesc& binding, ResolvedBinding& out);
ControlSample SampleResolvedPath(const InputManager& input, const ResolvedControlPath& path);
}
