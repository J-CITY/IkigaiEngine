#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "inputManager.h"
#include <mathModule/math.h>
#include <serdepp/attribute/default.hpp>
#include <serdepp/serializer.hpp>
#include <utilsModule/reflection/reflection.h>

namespace IKIGAI::ECS2 {
	class World;
}

namespace IKIGAI::INPUT_SYSTEM {
	inline constexpr const char* InputResourceExtension = ".input";
	inline constexpr const char* DefaultInputResourcePath = "/Configs/input.input";

	enum class InputPhase {
		Started,
		Performed,
		Canceled
	};

	enum class ActionValueType {
		Button,
		Float,
		Vector2
	};

	struct InputProcessorDesc {
		std::string type;
		float min = 0.0f;
		float max = 1.0f;
		float scale = 1.0f;
		bool invertX = false;
		bool invertY = false;

		template<class Context>
		constexpr static auto serde(Context& context, InputProcessorDesc& value) {
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&InputProcessorDesc::type, "type", default_{std::string{}})
				.field(&InputProcessorDesc::min, "min", default_{0.0f})
				.field(&InputProcessorDesc::max, "max", default_{1.0f})
				.field(&InputProcessorDesc::scale, "scale", default_{1.0f})
				.field(&InputProcessorDesc::invertX, "invertX", default_{false})
				.field(&InputProcessorDesc::invertY, "invertY", default_{false});
		}

		static auto GetMembers() {
			return std::tuple{
				UTILS::MakeMemberInfo("type", &InputProcessorDesc::type),
				UTILS::MakeMemberInfo("min", &InputProcessorDesc::min),
				UTILS::MakeMemberInfo("max", &InputProcessorDesc::max),
				UTILS::MakeMemberInfo("scale", &InputProcessorDesc::scale),
				UTILS::MakeMemberInfo("invertX", &InputProcessorDesc::invertX),
				UTILS::MakeMemberInfo("invertY", &InputProcessorDesc::invertY),
			};
		}
	};

	struct InputBindingDesc {
		std::string path;
		std::string composite;
		std::string up;
		std::string down;
		std::string left;
		std::string right;
		std::string positive;
		std::string negative;
		std::string interaction = "Press";
		float duration = 0.4f;
		float maxDuration = 0.2f;
		std::vector<InputProcessorDesc> processors;

		template<class Context>
		constexpr static auto serde(Context& context, InputBindingDesc& value) {
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&InputBindingDesc::path, "path", default_{std::string{}})
				.field(&InputBindingDesc::composite, "composite", default_{std::string{}})
				.field(&InputBindingDesc::up, "up", default_{std::string{}})
				.field(&InputBindingDesc::down, "down", default_{std::string{}})
				.field(&InputBindingDesc::left, "left", default_{std::string{}})
				.field(&InputBindingDesc::right, "right", default_{std::string{}})
				.field(&InputBindingDesc::positive, "positive", default_{std::string{}})
				.field(&InputBindingDesc::negative, "negative", default_{std::string{}})
				.field(&InputBindingDesc::interaction, "interaction", default_{std::string{"Press"}})
				.field(&InputBindingDesc::duration, "duration", default_{0.4f})
				.field(&InputBindingDesc::maxDuration, "maxDuration", default_{0.2f})
				.field(&InputBindingDesc::processors, "processors", default_{std::vector<InputProcessorDesc>{}});
		}

		static auto GetMembers() {
			return std::tuple{
				UTILS::MakeMemberInfo("path", &InputBindingDesc::path),
				UTILS::MakeMemberInfo("composite", &InputBindingDesc::composite),
				UTILS::MakeMemberInfo("up", &InputBindingDesc::up),
				UTILS::MakeMemberInfo("down", &InputBindingDesc::down),
				UTILS::MakeMemberInfo("left", &InputBindingDesc::left),
				UTILS::MakeMemberInfo("right", &InputBindingDesc::right),
				UTILS::MakeMemberInfo("positive", &InputBindingDesc::positive),
				UTILS::MakeMemberInfo("negative", &InputBindingDesc::negative),
				UTILS::MakeMemberInfo("interaction", &InputBindingDesc::interaction),
				UTILS::MakeMemberInfo("duration", &InputBindingDesc::duration),
				UTILS::MakeMemberInfo("maxDuration", &InputBindingDesc::maxDuration),
				UTILS::MakeMemberInfo("processors", &InputBindingDesc::processors),
			};
		}
	};

	struct InputActionDesc {
		std::string name;
		std::string type = "Button";
		std::string valueType;
		std::vector<InputBindingDesc> bindings;

		template<class Context>
		constexpr static auto serde(Context& context, InputActionDesc& value) {
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&InputActionDesc::name, "name", default_{std::string{}})
				.field(&InputActionDesc::type, "type", default_{std::string{"Button"}})
				.field(&InputActionDesc::valueType, "valueType", default_{std::string{}})
				.field(&InputActionDesc::bindings, "bindings", default_{std::vector<InputBindingDesc>{}});
		}

		static auto GetMembers() {
			return std::tuple{
				UTILS::MakeMemberInfo("name", &InputActionDesc::name),
				UTILS::MakeMemberInfo("type", &InputActionDesc::type),
				UTILS::MakeMemberInfo("valueType", &InputActionDesc::valueType),
				UTILS::MakeMemberInfo("bindings", &InputActionDesc::bindings),
			};
		}
	};

	struct InputMapDesc {
		std::string name;
		bool enabled = true;
		std::vector<InputActionDesc> actions;

		template<class Context>
		constexpr static auto serde(Context& context, InputMapDesc& value) {
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&InputMapDesc::name, "name", default_{std::string{}})
				.field(&InputMapDesc::enabled, "enabled", default_{true})
				.field(&InputMapDesc::actions, "actions", default_{std::vector<InputActionDesc>{}});
		}

		static auto GetMembers() {
			return std::tuple{
				UTILS::MakeMemberInfo("name", &InputMapDesc::name),
				UTILS::MakeMemberInfo("enabled", &InputMapDesc::enabled),
				UTILS::MakeMemberInfo("actions", &InputMapDesc::actions),
			};
		}
	};

	struct InputConfig {
		std::vector<InputMapDesc> maps;

		template<class Context>
		constexpr static auto serde(Context& context, InputConfig& value) {
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&InputConfig::maps, "maps", default_{std::vector<InputMapDesc>{}});
		}

		static auto GetMembers() {
			return std::tuple{
				UTILS::MakeMemberInfo("maps", &InputConfig::maps),
			};
		}
	};

	class InputActions {
	public:
		using Callback = std::function<void(const std::string& action, InputPhase phase)>;

		explicit InputActions(InputManager& inputManager);

		bool load(const std::string& path = DefaultInputResourcePath);
		void enableMap(const std::string& name, bool enabled);
		void prepareMaps(ECS2::World& world);
		void resolve(std::chrono::duration<double> dt);
		void subscribe(Callback callback);

		[[nodiscard]] bool isPressed(const std::string& action) const;
		[[nodiscard]] bool wasPressedThisFrame(const std::string& action) const;
		[[nodiscard]] bool wasReleasedThisFrame(const std::string& action) const;
		[[nodiscard]] float readValue(const std::string& action) const;
		[[nodiscard]] MATH::Vector2f readValue2(const std::string& action) const;
		[[nodiscard]] std::vector<InputPhase> getPhasesThisFrame(const std::string& action) const;
		[[nodiscard]] std::vector<std::string> getActionNames(const std::string& mapName) const;

	private:
		struct ActionState {
			ActionValueType valueType = ActionValueType::Button;
			std::string mapName;
			bool isPressed = false;
			bool wasPressedThisFrame = false;
			bool wasReleasedThisFrame = false;
			float value = 0.0f;
			MATH::Vector2f value2;
			std::vector<InputPhase> phases;
			std::vector<float> holdTimes;
		};

		struct MapState {
			InputMapDesc desc;
			bool assetEnabled = true;
			bool activeThisFrame = true;
		};

		InputManager& mInput;
		std::unordered_map<std::string, MapState> mMaps;
		std::unordered_map<std::string, ActionState> mActions;
		std::vector<Callback> mCallbacks;

		void rebuildActions();
		ActionState* findAction(const std::string& name);
		const ActionState* findAction(const std::string& name) const;
	};
}
