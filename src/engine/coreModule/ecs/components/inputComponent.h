#pragma once
#include <functional>
#include <string>

#include "component.h"
#include "utilsModule/reflection/reflection_macros.h"
#include "windowModule/inputManager/inputActions.h"

namespace IKIGAI::ECS { class Object; }

namespace IKIGAI::ECS {
	IKI_CLASS(Groups=[Component])
	class InputComponent : public ComponentBase {
		IKI_GENERATED_BODY(InputComponent)
		bool isActive = true;
		std::string mapName;
		std::function<void(std::chrono::duration<double>)> inputEventFun = [](std::chrono::duration<double>){};
		std::function<void(const std::string&, INPUT_SYSTEM::InputPhase)> actionCallback;
	public:
		IKI_CLASS(Name=InputComponent::Descriptor)
		struct Descriptor : public ComponentBase::Descriptor {
			IKI_GENERATED_BODY(InputComponent::Descriptor)
			IKI_PROPERTY(SEREALIZE(name="InputComponentType"))
			std::string Type;
			IKI_PROPERTY(SEREALIZE(name="MapName"))
			std::string MapName;
		};
		InputComponent(UTILS::Ref<ECS::Object> obj, std::function<void(std::chrono::duration<double>)> inputEventFun);
		InputComponent(UTILS::Ref<ECS::Object> obj);
		InputComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor) :
			InputComponent(obj) {
			mapName = static_cast<const Descriptor&>(descriptor).MapName;
		};
		void setActive(bool val);
		bool getActive() const;
		void setMapName(const std::string& name);
		const std::string& getMapName() const;
		void setActionCallback(std::function<void(const std::string&, INPUT_SYSTEM::InputPhase)> callback);
		const std::function<void(const std::string&, INPUT_SYSTEM::InputPhase)>& getActionCallback() const;
		const std::function<void(std::chrono::duration<double>)>& getEventFunc();
		[[nodiscard]] Descriptor getDescriptor() const;
	public:
		static auto GetMembers() {
			return std::tuple{
			};
		}
	};

	template <>
	inline std::string IKIGAI::ECS::GetType<InputComponent>() {
		return "class IKIGAI::ECS::InputComponent";
	}

	template <>
	inline std::string IKIGAI::ECS::GetComponentName<InputComponent>() {
		return "InputComponent";
	}
}

#include "generated/inputComponent.generated.h"
