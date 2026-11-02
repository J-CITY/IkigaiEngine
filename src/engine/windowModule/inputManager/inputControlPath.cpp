#include "inputControlPath.h"
#include "inputActions.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

namespace IKIGAI::INPUT_SYSTEM {
namespace {
	std::string ToLower(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	}

	std::optional<EKey> ParseKey(const std::string& name) {
		static const std::unordered_map<std::string, EKey> keys = { { "space", EKey::KEY_SPACE }, { "w", EKey::KEY_W }, { "a", EKey::KEY_A },
			{ "s", EKey::KEY_S }, { "d", EKey::KEY_D }, { "q", EKey::KEY_Q }, { "e", EKey::KEY_E }, { "r", EKey::KEY_R }, { "f", EKey::KEY_F },
			{ "g", EKey::KEY_G }, { "z", EKey::KEY_Z }, { "x", EKey::KEY_X }, { "c", EKey::KEY_C }, { "v", EKey::KEY_V },
			{ "escape", EKey::KEY_ESCAPE }, { "enter", EKey::KEY_ENTER }, { "tab", EKey::KEY_TAB }, { "backspace", EKey::KEY_BACKSPACE },
			{ "leftshift", EKey::KEY_LEFT_SHIFT }, { "rightshift", EKey::KEY_RIGHT_SHIFT }, { "leftcontrol", EKey::KEY_LEFT_CONTROL },
			{ "rightcontrol", EKey::KEY_RIGHT_CONTROL }, { "leftalt", EKey::KEY_LEFT_ALT }, { "rightalt", EKey::KEY_RIGHT_ALT },
			{ "up", EKey::KEY_UP }, { "down", EKey::KEY_DOWN }, { "left", EKey::KEY_LEFT }, { "right", EKey::KEY_RIGHT }, { "1", EKey::KEY_1 },
			{ "2", EKey::KEY_2 }, { "3", EKey::KEY_3 }, { "4", EKey::KEY_4 }, { "5", EKey::KEY_5 }, { "6", EKey::KEY_6 }, { "7", EKey::KEY_7 },
			{ "8", EKey::KEY_8 }, { "9", EKey::KEY_9 }, { "0", EKey::KEY_0 } };
		auto it = keys.find(ToLower(name));
		if (it == keys.end()) {
			return std::nullopt;
		}
		return it->second;
	}

	void ApplyProcessors(ControlSample& sample, const std::vector<InputProcessorDesc>& processors) {
		for (const auto& processor : processors) {
			const auto type = ToLower(processor.type);
			if (type == "deadzone") {
				if (sample.hasVec2) {
					const float len = std::sqrt(sample.value2.x * sample.value2.x + sample.value2.y * sample.value2.y);
					if (len < processor.min) {
						sample.value2 = {};
						sample.value = 0.0f;
						sample.down = false;
					}
				}
				else if (std::abs(sample.value) < processor.min) {
					sample.value = 0.0f;
					sample.down = false;
				}
			}
			else if (type == "invert") {
				sample.value = -sample.value;
				if (processor.invertX || !processor.invertY) {
					sample.value2.x = -sample.value2.x;
				}
				if (processor.invertY) {
					sample.value2.y = -sample.value2.y;
				}
			}
			else if (type == "scale") {
				sample.value *= processor.scale;
				sample.value2.x *= processor.scale;
				sample.value2.y *= processor.scale;
			}
			else if (type == "normalize" && sample.hasVec2) {
				const float len = std::sqrt(sample.value2.x * sample.value2.x + sample.value2.y * sample.value2.y);
				if (len > 0.0f) {
					sample.value2.x /= len;
					sample.value2.y /= len;
					sample.value = 1.0f;
				}
			}
			else if (type == "clamp") {
				sample.value = std::clamp(sample.value, processor.min, processor.max);
				sample.value2.x = std::clamp(sample.value2.x, processor.min, processor.max);
				sample.value2.y = std::clamp(sample.value2.y, processor.min, processor.max);
			}
		}
	}
}

bool ResolveControlPath(const std::string& rawPath, ResolvedControlPath& out) {
	out = {};
	if (rawPath.empty()) {
		return false;
	}
	auto path = ToLower(rawPath);
	const auto slash = path.find_last_of('/');
	const std::string device = slash == std::string::npos ? path : path.substr(0, slash);
	const std::string control = slash == std::string::npos ? std::string {} : path.substr(slash + 1);

	if (device.find("keyboard") != std::string::npos) {
		if (const auto key = ParseKey(control)) {
			out.type = ControlSourceType::KeyboardKey;
			out.key = *key;
			return true;
		}
		return false;
	}
	if (device.find("mouse") != std::string::npos) {
		if (control == "leftbutton") {
			out.type = ControlSourceType::MouseButton;
			out.mouseButton = EMouseButton::MOUSE_BUTTON_LEFT;
			return true;
		}
		if (control == "rightbutton") {
			out.type = ControlSourceType::MouseButton;
			out.mouseButton = EMouseButton::MOUSE_BUTTON_RIGHT;
			return true;
		}
		if (control == "middlebutton") {
			out.type = ControlSourceType::MouseButton;
			out.mouseButton = EMouseButton::MOUSE_BUTTON_MIDDLE;
			return true;
		}
		if (control == "delta") {
			out.type = ControlSourceType::MouseDelta;
			return true;
		}
		if (control == "position") {
			out.type = ControlSourceType::MousePosition;
			return true;
		}
		return false;
	}
	if (device.find("gamepad") != std::string::npos) {
		using Btn = INPUT::Gamepad::GAMEPAD_BUTTON;
		if (control == "buttonsouth" || control == "a") {
			out.type = ControlSourceType::GamepadButton;
			out.gamepadButton = Btn::btn_a;
			return true;
		}
		if (control == "buttoneast" || control == "b") {
			out.type = ControlSourceType::GamepadButton;
			out.gamepadButton = Btn::btn_b;
			return true;
		}
		if (control == "buttonwest" || control == "x") {
			out.type = ControlSourceType::GamepadButton;
			out.gamepadButton = Btn::btn_x;
			return true;
		}
		if (control == "buttonnorth" || control == "y") {
			out.type = ControlSourceType::GamepadButton;
			out.gamepadButton = Btn::btn_y;
			return true;
		}
		if (control == "leftshoulder") {
			out.type = ControlSourceType::GamepadButton;
			out.gamepadButton = Btn::btn_lb;
			return true;
		}
		if (control == "rightshoulder") {
			out.type = ControlSourceType::GamepadButton;
			out.gamepadButton = Btn::btn_rb;
			return true;
		}
		if (control == "dpadup") {
			out.type = ControlSourceType::GamepadButton;
			out.gamepadButton = Btn::dpad_up;
			return true;
		}
		if (control == "dpaddown") {
			out.type = ControlSourceType::GamepadButton;
			out.gamepadButton = Btn::dpad_down;
			return true;
		}
		if (control == "dpadleft") {
			out.type = ControlSourceType::GamepadButton;
			out.gamepadButton = Btn::dpad_left;
			return true;
		}
		if (control == "dpadright") {
			out.type = ControlSourceType::GamepadButton;
			out.gamepadButton = Btn::dpad_right;
			return true;
		}
		if (control == "start") {
			out.type = ControlSourceType::GamepadButton;
			out.gamepadButton = Btn::btn_start;
			return true;
		}
		if (control == "back") {
			out.type = ControlSourceType::GamepadButton;
			out.gamepadButton = Btn::btn_back;
			return true;
		}
		if (control == "leftstick") {
			out.type = ControlSourceType::GamepadLeftStick;
			return true;
		}
		if (control == "rightstick") {
			out.type = ControlSourceType::GamepadRightStick;
			return true;
		}
		if (control == "lefttrigger") {
			out.type = ControlSourceType::GamepadLeftTrigger;
			return true;
		}
		if (control == "righttrigger") {
			out.type = ControlSourceType::GamepadRightTrigger;
			return true;
		}
	}
	return false;
}

bool ResolveBinding(const InputBindingDesc& binding, ResolvedBinding& out) {
	out = {};
	out.interaction = binding.interaction.empty() ? "Press" : binding.interaction;
	out.duration = binding.duration;
	out.maxDuration = binding.maxDuration;
	if (!binding.composite.empty()) {
		const auto composite = ToLower(binding.composite);
		if (composite == "2dvector") {
			out.shape = BindingShape::Vector2;
			return ResolveControlPath(binding.up, out.up) && ResolveControlPath(binding.down, out.down) && ResolveControlPath(binding.left, out.left)
				&& ResolveControlPath(binding.right, out.right);
		}
		if (composite == "1daxis") {
			out.shape = BindingShape::Axis1;
			return ResolveControlPath(binding.positive, out.positive) && ResolveControlPath(binding.negative, out.negative);
		}
		return false;
	}
	out.shape = BindingShape::Single;
	return ResolveControlPath(binding.path, out.path);
}

ControlSample SampleResolvedPath(const InputManager& input, const ResolvedControlPath& path) {
	ControlSample sample;
	auto setButton = [&](bool down, bool pressed, bool released) {
		sample.down = down;
		sample.pressedThisFrame = pressed;
		sample.releasedThisFrame = released;
		sample.value = down ? 1.0f : 0.0f;
	};

	switch (path.type) {
	case ControlSourceType::KeyboardKey:
		setButton(input.isKeyPressed(path.key), input.wasKeyPressedThisFrame(path.key), input.wasKeyReleasedThisFrame(path.key));
		break;
	case ControlSourceType::MouseButton:
		setButton(input.isMouseButtonPressed(path.mouseButton), input.wasMouseButtonPressedThisFrame(path.mouseButton),
			input.wasMouseButtonReleasedThisFrame(path.mouseButton));
		break;
	case ControlSourceType::MouseDelta:
		sample.value2 = input.getMouseDelta();
		sample.hasVec2 = true;
		sample.value = std::max(std::abs(sample.value2.x), std::abs(sample.value2.y));
		break;
	case ControlSourceType::MousePosition: {
		const auto pos = input.getMousePosition();
		sample.value2 = { static_cast<float>(pos.x), static_cast<float>(pos.y) };
		sample.hasVec2 = true;
		sample.value = 1.0f;
		break;
	}
	case ControlSourceType::GamepadButton: {
		const auto* gp = input.getFirstGamepad();
		if (!gp) {
			break;
		}
		const int id = gp->getId();
		setButton(input.isButtonPressed(id, path.gamepadButton), input.wasButtonPressedThisFrame(id, path.gamepadButton),
			input.wasButtonReleasedThisFrame(id, path.gamepadButton));
		break;
	}
	case ControlSourceType::GamepadLeftStick:
	case ControlSourceType::GamepadRightStick:
	case ControlSourceType::GamepadLeftTrigger:
	case ControlSourceType::GamepadRightTrigger: {
		const auto* gp = input.getFirstGamepad();
		if (!gp) {
			break;
		}
		if (path.type == ControlSourceType::GamepadLeftStick) {
			sample.value2 = { gp->getData().mLeftSticX, gp->getData().mLeftSticY };
		}
		else if (path.type == ControlSourceType::GamepadRightStick) {
			sample.value2 = { gp->getData().mRightSticX, gp->getData().mRightSticY };
		}
		else if (path.type == ControlSourceType::GamepadLeftTrigger) {
			sample.value = gp->getData().mLeftTrigger;
			sample.down = sample.value > 0.2f;
		}
		else {
			sample.value = gp->getData().mRightTrigger;
			sample.down = sample.value > 0.2f;
		}
		if (path.type == ControlSourceType::GamepadLeftStick || path.type == ControlSourceType::GamepadRightStick) {
			sample.hasVec2 = true;
			sample.value = std::sqrt(sample.value2.x * sample.value2.x + sample.value2.y * sample.value2.y);
			sample.down = sample.value > 0.2f;
		}
		break;
	}
	case ControlSourceType::None:
	default:
		break;
	}
	return sample;
}

}
