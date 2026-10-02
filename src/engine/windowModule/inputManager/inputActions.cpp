#include "inputActions.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>

#include "coreModule/ecs/components/inputComponent.h"
#include "ecsModule/world.h"
#include "utilsModule/jsonLoader.h"
#include "utilsModule/log/loggerDefine.h"

namespace IKIGAI::INPUT_SYSTEM {
	namespace {
		std::string ToLower(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});
			return value;
		}

		std::optional<EKey> ParseKey(const std::string& name) {
			static const std::unordered_map<std::string, EKey> keys = {
				{"space", EKey::KEY_SPACE}, {"w", EKey::KEY_W}, {"a", EKey::KEY_A}, {"s", EKey::KEY_S}, {"d", EKey::KEY_D},
				{"q", EKey::KEY_Q}, {"e", EKey::KEY_E}, {"r", EKey::KEY_R}, {"f", EKey::KEY_F}, {"g", EKey::KEY_G},
				{"z", EKey::KEY_Z}, {"x", EKey::KEY_X}, {"c", EKey::KEY_C}, {"v", EKey::KEY_V},
				{"escape", EKey::KEY_ESCAPE}, {"enter", EKey::KEY_ENTER}, {"tab", EKey::KEY_TAB},
				{"backspace", EKey::KEY_BACKSPACE}, {"leftshift", EKey::KEY_LEFT_SHIFT}, {"rightshift", EKey::KEY_RIGHT_SHIFT},
				{"leftcontrol", EKey::KEY_LEFT_CONTROL}, {"rightcontrol", EKey::KEY_RIGHT_CONTROL},
				{"leftalt", EKey::KEY_LEFT_ALT}, {"rightalt", EKey::KEY_RIGHT_ALT},
				{"up", EKey::KEY_UP}, {"down", EKey::KEY_DOWN}, {"left", EKey::KEY_LEFT}, {"right", EKey::KEY_RIGHT},
				{"1", EKey::KEY_1}, {"2", EKey::KEY_2}, {"3", EKey::KEY_3}, {"4", EKey::KEY_4}, {"5", EKey::KEY_5},
				{"6", EKey::KEY_6}, {"7", EKey::KEY_7}, {"8", EKey::KEY_8}, {"9", EKey::KEY_9}, {"0", EKey::KEY_0}
			};
			auto it = keys.find(ToLower(name));
			if (it == keys.end()) {
				return std::nullopt;
			}
			return it->second;
		}

		struct ControlSample {
			bool down = false;
			bool pressedThisFrame = false;
			bool releasedThisFrame = false;
			float value = 0.0f;
			MATH::Vector2f value2;
			bool hasVec2 = false;
		};

		ControlSample SamplePath(const InputManager& input, const std::string& rawPath) {
			ControlSample sample;
			if (rawPath.empty()) {
				return sample;
			}
			auto path = ToLower(rawPath);
			const auto slash = path.find_last_of('/');
			const std::string device = slash == std::string::npos ? path : path.substr(0, slash);
			const std::string control = slash == std::string::npos ? std::string{} : path.substr(slash + 1);

			auto setButton = [&](bool down, bool pressed, bool released) {
				sample.down = down;
				sample.pressedThisFrame = pressed;
				sample.releasedThisFrame = released;
				sample.value = down ? 1.0f : 0.0f;
			};

			if (device.find("keyboard") != std::string::npos) {
				if (const auto key = ParseKey(control)) {
					setButton(input.isKeyPressed(*key), input.wasKeyPressedThisFrame(*key), input.wasKeyReleasedThisFrame(*key));
				}
				return sample;
			}
			if (device.find("mouse") != std::string::npos) {
				if (control == "leftbutton") {
					setButton(input.isMouseButtonPressed(EMouseButton::MOUSE_BUTTON_LEFT),
						input.wasMouseButtonPressedThisFrame(EMouseButton::MOUSE_BUTTON_LEFT),
						input.wasMouseButtonReleasedThisFrame(EMouseButton::MOUSE_BUTTON_LEFT));
				} else if (control == "rightbutton") {
					setButton(input.isMouseButtonPressed(EMouseButton::MOUSE_BUTTON_RIGHT),
						input.wasMouseButtonPressedThisFrame(EMouseButton::MOUSE_BUTTON_RIGHT),
						input.wasMouseButtonReleasedThisFrame(EMouseButton::MOUSE_BUTTON_RIGHT));
				} else if (control == "middlebutton") {
					setButton(input.isMouseButtonPressed(EMouseButton::MOUSE_BUTTON_MIDDLE),
						input.wasMouseButtonPressedThisFrame(EMouseButton::MOUSE_BUTTON_MIDDLE),
						input.wasMouseButtonReleasedThisFrame(EMouseButton::MOUSE_BUTTON_MIDDLE));
				} else if (control == "delta") {
					sample.value2 = input.getMouseDelta();
					sample.hasVec2 = true;
					sample.value = std::max(std::abs(sample.value2.x), std::abs(sample.value2.y));
				} else if (control == "position") {
					const auto pos = input.getMousePosition();
					sample.value2 = {static_cast<float>(pos.x), static_cast<float>(pos.y)};
					sample.hasVec2 = true;
					sample.value = 1.0f;
				}
				return sample;
			}
			if (device.find("gamepad") != std::string::npos) {
				const auto* gp = input.getFirstGamepad();
				if (!gp) {
					return sample;
				}
				const int id = gp->getId();
				using Btn = INPUT::Gamepad::GAMEPAD_BUTTON;
				auto setGpButton = [&](Btn btn) {
					setButton(input.isButtonPressed(id, btn), input.wasButtonPressedThisFrame(id, btn), input.wasButtonReleasedThisFrame(id, btn));
				};
				if (control == "buttonsouth" || control == "a") setGpButton(Btn::btn_a);
				else if (control == "buttoneast" || control == "b") setGpButton(Btn::btn_b);
				else if (control == "buttonwest" || control == "x") setGpButton(Btn::btn_x);
				else if (control == "buttonnorth" || control == "y") setGpButton(Btn::btn_y);
				else if (control == "leftshoulder") setGpButton(Btn::btn_lb);
				else if (control == "rightshoulder") setGpButton(Btn::btn_rb);
				else if (control == "dpadup") setGpButton(Btn::dpad_up);
				else if (control == "dpaddown") setGpButton(Btn::dpad_down);
				else if (control == "dpadleft") setGpButton(Btn::dpad_left);
				else if (control == "dpadright") setGpButton(Btn::dpad_right);
				else if (control == "start") setGpButton(Btn::btn_start);
				else if (control == "back") setGpButton(Btn::btn_back);
				else if (control == "leftstick") {
					sample.value2 = {gp->getData().mLeftSticX, gp->getData().mLeftSticY};
					sample.hasVec2 = true;
					sample.value = std::sqrt(sample.value2.x * sample.value2.x + sample.value2.y * sample.value2.y);
					sample.down = sample.value > 0.2f;
				} else if (control == "rightstick") {
					sample.value2 = {gp->getData().mRightSticX, gp->getData().mRightSticY};
					sample.hasVec2 = true;
					sample.value = std::sqrt(sample.value2.x * sample.value2.x + sample.value2.y * sample.value2.y);
					sample.down = sample.value > 0.2f;
				} else if (control == "lefttrigger") {
					sample.value = gp->getData().mLeftTrigger;
					sample.down = sample.value > 0.2f;
				} else if (control == "righttrigger") {
					sample.value = gp->getData().mRightTrigger;
					sample.down = sample.value > 0.2f;
				}
			}
			return sample;
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
					} else if (std::abs(sample.value) < processor.min) {
						sample.value = 0.0f;
						sample.down = false;
					}
				} else if (type == "invert") {
					sample.value = -sample.value;
					if (processor.invertX || !processor.invertY) {
						sample.value2.x = -sample.value2.x;
					}
					if (processor.invertY) {
						sample.value2.y = -sample.value2.y;
					}
				} else if (type == "scale") {
					sample.value *= processor.scale;
					sample.value2.x *= processor.scale;
					sample.value2.y *= processor.scale;
				} else if (type == "normalize" && sample.hasVec2) {
					const float len = std::sqrt(sample.value2.x * sample.value2.x + sample.value2.y * sample.value2.y);
					if (len > 0.0f) {
						sample.value2.x /= len;
						sample.value2.y /= len;
						sample.value = 1.0f;
					}
				} else if (type == "clamp") {
					sample.value = std::clamp(sample.value, processor.min, processor.max);
					sample.value2.x = std::clamp(sample.value2.x, processor.min, processor.max);
					sample.value2.y = std::clamp(sample.value2.y, processor.min, processor.max);
				}
			}
		}

		ControlSample SampleBinding(const InputManager& input, const InputBindingDesc& binding) {
			if (!binding.composite.empty()) {
				ControlSample sample;
				const auto composite = ToLower(binding.composite);
				if (composite == "2dvector") {
					const float up = SamplePath(input, binding.up).value;
					const float down = SamplePath(input, binding.down).value;
					const float left = SamplePath(input, binding.left).value;
					const float right = SamplePath(input, binding.right).value;
					sample.value2 = {right - left, up - down};
					sample.hasVec2 = true;
					sample.value = std::max(std::abs(sample.value2.x), std::abs(sample.value2.y));
					sample.down = sample.value > 0.0f;
				} else if (composite == "1daxis") {
					sample.value = SamplePath(input, binding.positive).value - SamplePath(input, binding.negative).value;
					sample.down = std::abs(sample.value) > 0.0f;
				}
				ApplyProcessors(sample, binding.processors);
				return sample;
			}
			auto sample = SamplePath(input, binding.path);
			ApplyProcessors(sample, binding.processors);
			return sample;
		}
	}

	InputActions::InputActions(InputManager& inputManager) : mInput(inputManager) {}

	bool InputActions::load(const std::string& path) {
		auto result = UTILS::FromJson<InputConfig>(path);
		if (result.isErr()) {
			LOG_ERROR << "Failed to load input resource '" << path << "': " << result.unwrapErr().text;
			return false;
		}
		mMaps.clear();
		for (auto& map : result.unwrap().maps) {
			MapState state;
			state.desc = std::move(map);
			state.assetEnabled = state.desc.enabled;
			state.activeThisFrame = state.desc.enabled;
			mMaps[state.desc.name] = std::move(state);
		}
		rebuildActions();
		return true;
	}

	void InputActions::enableMap(const std::string& name, bool enabled) {
		auto it = mMaps.find(name);
		if (it != mMaps.end()) {
			it->second.assetEnabled = enabled;
		}
	}

	void InputActions::prepareMaps(ECS2::World& world) {
		for (auto& [name, map] : mMaps) {
			map.activeThisFrame = map.assetEnabled;
		}
		auto* components = world.getComponentManager();
		if (!components) {
			return;
		}
		for (auto& component : components->getComponentsArray<ECS::InputComponent>()) {
			if (component.getActive() && !component.getMapName().empty()) {
				if (auto it = mMaps.find(component.getMapName()); it != mMaps.end()) {
					it->second.activeThisFrame = true;
				}
			}
		}
	}

	void InputActions::resolve(std::chrono::duration<double> dt) {
		const float delta = static_cast<float>(dt.count());
		for (auto& [name, action] : mActions) {
			action.wasPressedThisFrame = false;
			action.wasReleasedThisFrame = false;
			action.phases.clear();
			action.value = 0.0f;
			action.value2 = {};

			const auto mapIt = mMaps.find(action.mapName);
			if (mapIt == mMaps.end() || !mapIt->second.activeThisFrame) {
				if (action.isPressed) {
					action.isPressed = false;
					action.wasReleasedThisFrame = true;
					action.phases.push_back(InputPhase::Canceled);
				}
				continue;
			}

			const auto& desc = [&]() -> const InputActionDesc* {
				for (const auto& candidate : mapIt->second.desc.actions) {
					if (candidate.name == name) {
						return &candidate;
					}
				}
				return nullptr;
			}();
			if (!desc) {
				continue;
			}

			if (action.holdTimes.size() != desc->bindings.size()) {
				action.holdTimes.assign(desc->bindings.size(), 0.0f);
			}

			bool anyDown = false;
			bool performed = false;
			bool canceled = false;
			float bestMag = -1.0f;
			for (size_t i = 0; i < desc->bindings.size(); ++i) {
				const auto& binding = desc->bindings[i];
				const auto sample = SampleBinding(mInput, binding);
				const auto interaction = ToLower(binding.interaction.empty() ? "press" : binding.interaction);
				if (sample.hasVec2 && sample.value > bestMag) {
					bestMag = sample.value;
					action.value2 = sample.value2;
					action.value = sample.value;
				} else if (!sample.hasVec2 && std::abs(sample.value) > bestMag) {
					bestMag = std::abs(sample.value);
					action.value = sample.value;
				}

				if (interaction == "hold") {
					if (sample.down) {
						action.holdTimes[i] += delta;
						anyDown = action.holdTimes[i] >= binding.duration;
						if (action.holdTimes[i] >= binding.duration && action.holdTimes[i] - delta < binding.duration) {
							performed = true;
						}
					} else {
						if (action.holdTimes[i] > 0.0f && action.holdTimes[i] < binding.duration) {
							canceled = true;
						}
						action.holdTimes[i] = 0.0f;
					}
				} else if (interaction == "tap") {
					if (sample.down) {
						action.holdTimes[i] += delta;
						if (action.holdTimes[i] > binding.maxDuration) {
							canceled = true;
						}
					} else if (sample.releasedThisFrame && action.holdTimes[i] > 0.0f && action.holdTimes[i] <= binding.maxDuration) {
						performed = true;
						action.holdTimes[i] = 0.0f;
					} else {
						action.holdTimes[i] = 0.0f;
					}
				} else {
					anyDown = anyDown || sample.down;
					performed = performed || sample.pressedThisFrame;
					canceled = canceled || sample.releasedThisFrame;
				}
			}

			const bool wasPressed = action.isPressed;
			if (action.valueType == ActionValueType::Button) {
				action.isPressed = anyDown;
			} else {
				action.isPressed = std::abs(action.value) > 0.1f || (action.value2.x * action.value2.x + action.value2.y * action.value2.y) > 0.01f;
			}
			action.wasPressedThisFrame = performed || (!wasPressed && action.isPressed);
			action.wasReleasedThisFrame = canceled || (wasPressed && !action.isPressed);

			if (!wasPressed && action.isPressed) {
				action.phases.push_back(InputPhase::Started);
			}
			if (action.wasPressedThisFrame) {
				action.phases.push_back(InputPhase::Performed);
			}
			if (action.wasReleasedThisFrame) {
				action.phases.push_back(InputPhase::Canceled);
			}

			for (auto phase : action.phases) {
				for (auto& callback : mCallbacks) {
					callback(name, phase);
				}
			}
		}
	}

	void InputActions::subscribe(Callback callback) {
		mCallbacks.push_back(std::move(callback));
	}

	bool InputActions::isPressed(const std::string& action) const {
		const auto* state = findAction(action);
		return state && state->isPressed;
	}

	bool InputActions::wasPressedThisFrame(const std::string& action) const {
		const auto* state = findAction(action);
		return state && state->wasPressedThisFrame;
	}

	bool InputActions::wasReleasedThisFrame(const std::string& action) const {
		const auto* state = findAction(action);
		return state && state->wasReleasedThisFrame;
	}

	float InputActions::readValue(const std::string& action) const {
		const auto* state = findAction(action);
		return state ? state->value : 0.0f;
	}

	MATH::Vector2f InputActions::readValue2(const std::string& action) const {
		const auto* state = findAction(action);
		return state ? state->value2 : MATH::Vector2f{};
	}

	std::vector<InputPhase> InputActions::getPhasesThisFrame(const std::string& action) const {
		const auto* state = findAction(action);
		return state ? state->phases : std::vector<InputPhase>{};
	}

	std::vector<std::string> InputActions::getActionNames(const std::string& mapName) const {
		std::vector<std::string> names;
		const auto it = mMaps.find(mapName);
		if (it == mMaps.end()) {
			return names;
		}
		for (const auto& action : it->second.desc.actions) {
			names.push_back(action.name);
		}
		return names;
	}

	void InputActions::rebuildActions() {
		mActions.clear();
		for (const auto& [mapName, map] : mMaps) {
			for (const auto& action : map.desc.actions) {
				ActionState state;
				state.mapName = mapName;
				const auto type = ToLower(action.type);
				const auto valueType = ToLower(action.valueType);
				if (type == "value" && (valueType == "vector2" || valueType == "vec2")) {
					state.valueType = ActionValueType::Vector2;
				} else if (type == "value") {
					state.valueType = ActionValueType::Float;
				}
				state.holdTimes.assign(action.bindings.size(), 0.0f);
				mActions[action.name] = std::move(state);
			}
		}
	}

	InputActions::ActionState* InputActions::findAction(const std::string& name) {
		auto it = mActions.find(name);
		return it == mActions.end() ? nullptr : &it->second;
	}

	const InputActions::ActionState* InputActions::findAction(const std::string& name) const {
		auto it = mActions.find(name);
		return it == mActions.end() ? nullptr : &it->second;
	}
}
