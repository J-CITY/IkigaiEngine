#include "inputActions.h"

#include "coreModule/ecs/components/inputComponent.h"
#include "ecsModule/world.h"
#include "inputControlPath.h"
#include "resourceModule/playerProfile.h"
#include "resourceModule/serviceManager.h"
#include "utilsModule/jsonLoader.h"
#include "utilsModule/log/loggerDefine.h"
#include <algorithm>
#include <cctype>
#include <optional>

namespace IKIGAI::INPUT_SYSTEM {
namespace {
	std::string ToLower(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	}

	ControlSample SampleResolvedBinding(
		const InputManager& input, const ResolvedBinding& binding, const std::vector<InputProcessorDesc>& processors) {
		ControlSample sample;
		if (binding.shape == BindingShape::Vector2) {
			const float up = SampleResolvedPath(input, binding.up).value;
			const float down = SampleResolvedPath(input, binding.down).value;
			const float left = SampleResolvedPath(input, binding.left).value;
			const float right = SampleResolvedPath(input, binding.right).value;
			sample.value2 = { right - left, up - down };
			sample.hasVec2 = true;
			sample.value = std::max(std::abs(sample.value2.x), std::abs(sample.value2.y));
			sample.down = sample.value > 0.0f;
		}
		else if (binding.shape == BindingShape::Axis1) {
			sample.value = SampleResolvedPath(input, binding.positive).value - SampleResolvedPath(input, binding.negative).value;
			sample.down = std::abs(sample.value) > 0.0f;
		}
		else {
			sample = SampleResolvedPath(input, binding.path);
		}

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
		return sample;
	}
}

InputActions::InputActions(InputManager& inputManager)
	: mInput(inputManager) { }

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

bool InputActions::loadBindingOverrides(const std::string& path) {
	std::optional<InputBindingOverrides> loaded;
	if (RESOURCES::ServiceManager::Check<RESOURCES::PlayerProfileService>()) {
		const auto bytes = RESOURCES::ServiceManager::Get<RESOURCES::PlayerProfileService>().profile().readFile(path);
		if (!bytes.empty()) {
			if (auto parsed = UTILS::FromJsonStr<InputBindingOverrides>(std::string(bytes.begin(), bytes.end())); parsed.isOk()) {
				loaded = parsed.unwrap();
			}
		}
	}
	if (!loaded) {
		if (auto parsed = UTILS::FromJson<InputBindingOverrides>(path); parsed.isOk()) {
			loaded = parsed.unwrap();
		}
	}
	if (!loaded) {
		LOG_WARNING << "No input binding overrides loaded from '" << path << "'";
		return false;
	}
	mBindingOverrides.clear();
	for (const auto& entry : loaded->overrides) {
		mBindingOverrides[{ entry.map, entry.action, entry.bindingIndex }] = entry.binding;
	}
	rebuildActions();
	return true;
}

bool InputActions::saveBindingOverrides(const std::string& path) const {
	InputBindingOverrides payload;
	for (const auto& [key, binding] : mBindingOverrides) {
		payload.overrides.push_back({ key.map, key.action, key.bindingIndex, binding });
	}
	auto result = UTILS::ToJsonStr(payload, 2);
	if (result.isErr()) {
		LOG_ERROR << "Failed to serialize input binding overrides: " << result.unwrapErr().text;
		return false;
	}
	if (!RESOURCES::ServiceManager::Check<RESOURCES::PlayerProfileService>()) {
		LOG_ERROR << "PlayerProfileService unavailable; cannot save binding overrides to '" << path << "'";
		return false;
	}
	const auto& text = result.unwrap();
	std::vector<uint8_t> bytes(text.begin(), text.end());
	RESOURCES::ServiceManager::Get<RESOURCES::PlayerProfileService>().profile().writeFile(path, bytes).wait();
	return true;
}

void InputActions::enableMap(const std::string& name, bool enabled) {
	auto it = mMaps.find(name);
	if (it != mMaps.end()) {
		it->second.assetEnabled = enabled;
	}
}

void InputActions::prepareMaps(ECS::World& world) {
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
	for (std::size_t actionIndex = 0; actionIndex < mActionRecords.size(); ++actionIndex) {
		auto& record = mActionRecords[actionIndex];
		auto& action = mActionStates[actionIndex];
		action.wasPressedThisFrame = false;
		action.wasReleasedThisFrame = false;
		action.phases.clear();
		action.value = 0.0f;
		action.value2 = {};

		const auto mapIt = mMaps.find(record.mapName);
		if (mapIt == mMaps.end() || !mapIt->second.activeThisFrame || record.desc == nullptr) {
			if (action.isPressed) {
				action.isPressed = false;
				action.wasReleasedThisFrame = true;
				action.phases.push_back(InputPhase::Canceled);
			}
			continue;
		}

		if (action.holdTimes.size() != record.bindings.size()) {
			action.holdTimes.assign(record.bindings.size(), 0.0f);
		}

		bool anyDown = false;
		bool performed = false;
		bool canceled = false;
		float bestMag = -1.0f;
		for (std::size_t i = 0; i < record.bindings.size(); ++i) {
			const auto& binding = record.bindings[i];
			const auto& processors = i < record.bindingProcessors.size() ? record.bindingProcessors[i] : std::vector<InputProcessorDesc> {};
			const auto sample = SampleResolvedBinding(mInput, binding, processors);
			const auto interaction = ToLower(binding.interaction.empty() ? "press" : binding.interaction);
			if (sample.hasVec2 && sample.value > bestMag) {
				bestMag = sample.value;
				action.value2 = sample.value2;
				action.value = sample.value;
			}
			else if (!sample.hasVec2 && std::abs(sample.value) > bestMag) {
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
				}
				else {
					if (action.holdTimes[i] > 0.0f && action.holdTimes[i] < binding.duration) {
						canceled = true;
					}
					action.holdTimes[i] = 0.0f;
				}
			}
			else if (interaction == "tap") {
				if (sample.down) {
					action.holdTimes[i] += delta;
					if (action.holdTimes[i] > binding.maxDuration) {
						canceled = true;
					}
				}
				else if (sample.releasedThisFrame && action.holdTimes[i] > 0.0f && action.holdTimes[i] <= binding.maxDuration) {
					performed = true;
					action.holdTimes[i] = 0.0f;
				}
				else {
					action.holdTimes[i] = 0.0f;
				}
			}
			else {
				anyDown = anyDown || sample.down;
				performed = performed || sample.pressedThisFrame;
				canceled = canceled || sample.releasedThisFrame;
			}
		}

		const bool wasPressed = action.isPressed;
		if (action.valueType == ActionValueType::Button) {
			action.isPressed = anyDown;
		}
		else {
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
				callback(record.id, record.name, phase);
			}
		}
	}
}

void InputActions::subscribe(Callback callback) {
	mCallbacks.push_back(std::move(callback));
}

ActionId InputActions::findActionId(const std::string& action) const {
	const auto it = mActionIds.find(action);
	return it == mActionIds.end() ? InvalidActionId : it->second;
}

const std::string& InputActions::getActionName(ActionId id) const {
	if (id < mActionRecords.size()) {
		return mActionRecords[id].name;
	}
	static const std::string kEmpty;
	return kEmpty;
}

bool InputActions::isPressed(ActionId id) const {
	const auto* state = findActionState(id);
	return state && state->isPressed;
}

bool InputActions::wasPressedThisFrame(ActionId id) const {
	const auto* state = findActionState(id);
	return state && state->wasPressedThisFrame;
}

bool InputActions::wasReleasedThisFrame(ActionId id) const {
	const auto* state = findActionState(id);
	return state && state->wasReleasedThisFrame;
}

float InputActions::readValue(ActionId id) const {
	const auto* state = findActionState(id);
	return state ? state->value : 0.0f;
}

MATH::Vector2f InputActions::readValue2(ActionId id) const {
	const auto* state = findActionState(id);
	return state ? state->value2 : MATH::Vector2f {};
}

std::vector<InputPhase> InputActions::getPhasesThisFrame(ActionId id) const {
	const auto* state = findActionState(id);
	return state ? state->phases : std::vector<InputPhase> {};
}

bool InputActions::isPressed(const std::string& action) const {
	return isPressed(findActionId(action));
}

bool InputActions::wasPressedThisFrame(const std::string& action) const {
	return wasPressedThisFrame(findActionId(action));
}

bool InputActions::wasReleasedThisFrame(const std::string& action) const {
	return wasReleasedThisFrame(findActionId(action));
}

float InputActions::readValue(const std::string& action) const {
	return readValue(findActionId(action));
}

MATH::Vector2f InputActions::readValue2(const std::string& action) const {
	return readValue2(findActionId(action));
}

std::vector<InputPhase> InputActions::getPhasesThisFrame(const std::string& action) const {
	return getPhasesThisFrame(findActionId(action));
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

std::vector<ActionId> InputActions::getActionIds(const std::string& mapName) const {
	std::vector<ActionId> ids;
	for (const auto& record : mActionRecords) {
		if (record.mapName == mapName) {
			ids.push_back(record.id);
		}
	}
	return ids;
}

bool InputActions::rebindBinding(const std::string& mapName, const std::string& actionName, std::size_t bindingIndex, InputBindingDesc binding) {
	mBindingOverrides[{ mapName, actionName, bindingIndex }] = std::move(binding);
	rebuildActions();
	return true;
}

bool InputActions::resetBinding(const std::string& mapName, const std::string& actionName, std::size_t bindingIndex) {
	mBindingOverrides.erase({ mapName, actionName, bindingIndex });
	rebuildActions();
	return true;
}

void InputActions::clearBindingOverrides() {
	mBindingOverrides.clear();
	rebuildActions();
}

void InputActions::rebuildActions() {
	std::vector<ActionState> previousStates;
	previousStates.swap(mActionStates);

	mActionRecords.clear();
	mActionStates.clear();
	mActionIds.clear();

	ActionId nextId = 0;
	for (const auto& [mapName, map] : mMaps) {
		for (const auto& action : map.desc.actions) {
			ActionRecord record;
			record.id = nextId++;
			record.name = action.name;
			record.mapName = mapName;
			record.desc = &action;

			const auto type = ToLower(action.type);
			const auto valueType = ToLower(action.valueType);
			if (type == "value" && (valueType == "vector2" || valueType == "vec2")) {
				record.valueType = ActionValueType::Vector2;
			}
			else if (type == "value") {
				record.valueType = ActionValueType::Float;
			}

			record.bindings.reserve(action.bindings.size());
			record.bindingProcessors.reserve(action.bindings.size());
			for (std::size_t bindingIndex = 0; bindingIndex < action.bindings.size(); ++bindingIndex) {
				const auto bindingDesc = bindingFor(record, bindingIndex);
				ResolvedBinding resolved;
				if (ResolveBinding(bindingDesc, resolved)) {
					record.bindings.push_back(resolved);
					record.bindingProcessors.push_back(bindingDesc.processors);
				}
			}

			const ActionId assignedId = record.id;
			mActionIds[record.name] = assignedId;
			mActionRecords.push_back(std::move(record));

			ActionState state;
			state.mapName = mapName;
			state.valueType = mActionRecords.back().valueType;
			if (assignedId < previousStates.size() && previousStates[assignedId].mapName == mapName) {
				state = previousStates[assignedId];
				state.valueType = mActionRecords.back().valueType;
			}
			state.holdTimes.assign(mActionRecords.back().bindings.size(), 0.0f);
			mActionStates.push_back(std::move(state));
		}
	}
}

InputBindingDesc InputActions::bindingFor(const ActionRecord& record, std::size_t bindingIndex) const {
	if (record.desc == nullptr || bindingIndex >= record.desc->bindings.size()) {
		return {};
	}
	const BindingOverrideKey key { record.mapName, record.name, bindingIndex };
	if (const auto it = mBindingOverrides.find(key); it != mBindingOverrides.end()) {
		return it->second;
	}
	return record.desc->bindings[bindingIndex];
}

const InputActions::ActionState* InputActions::findActionState(ActionId id) const {
	return id < mActionStates.size() ? &mActionStates[id] : nullptr;
}

InputActions::ActionState* InputActions::findActionState(ActionId id) {
	return id < mActionStates.size() ? &mActionStates[id] : nullptr;
}
}
