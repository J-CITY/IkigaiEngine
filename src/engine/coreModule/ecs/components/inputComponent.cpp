#include "inputComponent.h"

#include "transform.h"

using namespace IKIGAI::ECS;

InputComponent::InputComponent(UTILS::Ref<ECS::Object> obj, std::function<void(std::chrono::duration<double>)> _inputEventFun):
	ComponentBase(obj) {
	inputEventFun = _inputEventFun;
	__NAME__ = "InputComponent";
}

InputComponent::InputComponent(UTILS::Ref<ECS::Object> obj) :
	ComponentBase(obj) {
	__NAME__ = "InputComponent";
}

void InputComponent::setActive(bool val) {
	isActive = val;
}

bool InputComponent::getActive() const {
	return isActive;
}

const std::function<void(std::chrono::duration<double>)>& InputComponent::getEventFunc() {
	return inputEventFun;
}

void InputComponent::setMapName(const std::string& name) {
	mapName = name;
}

const std::string& InputComponent::getMapName() const {
	return mapName;
}

void InputComponent::setActionCallback(std::function<void(const std::string&, INPUT_SYSTEM::InputPhase)> callback) {
	actionCallback = std::move(callback);
}

const std::function<void(const std::string&, IKIGAI::INPUT_SYSTEM::InputPhase)>& InputComponent::getActionCallback() const {
	return actionCallback;
}

InputComponent::Descriptor InputComponent::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<InputComponent>();
	descriptor.MapName = mapName;
	return descriptor;
}
//#include <rttr/registration>
//
//RTTR_REGISTRATION
//{
//	rttr::registration::class_<IKIGAI::ECS::InputComponent>("InputComponent")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	);
//}