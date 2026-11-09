#pragma once

#ifdef USE_EDITOR

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "utilsModule/animation.h"
#include "utilsModule/imguiHelper/imguiWidgets.h"
#include <magic_enum.hpp>
#include <map>
#include <string>

struct Prop {
	Prop() = default;
	Prop(std::string objName, IKIGAI::ECS::Object::Id objId, std::string componentName, std::string propertyName,
		IKIGAI::ANIMATION::AnimationProperty animProp, IKIGAI::ANIMATION::PropType defaultValue)
		: objName(objName)
		, objId(objId)
		, componentName(componentName)
		, propertyName(propertyName)
		, animProp(animProp)
		, defaultValue(defaultValue) { }

	std::string objName;
	IKIGAI::ECS::Object::Id objId { IKIGAI::ECS::Entity::ID(0) };
	std::string componentName;
	std::string propertyName;
	IKIGAI::ANIMATION::AnimationProperty animProp;
	IKIGAI::ANIMATION::PropType defaultValue;
};

inline std::map<std::string, Prop> AllObjPropsInAnimation;
inline std::map<std::string, Prop> ObjProps;

template <class CLASS, class PTR> void widgetFloat4(CLASS* comp, IKIGAI::UTILS::MemberInfo<CLASS, PTR>& prop) {
	const std::string& propName = prop.getName();
	if (!IKIGAI::IMGUI::CombineVecEdit::Data.contains(propName)) {
		IKIGAI::IMGUI::CombineVecEdit::Data.insert(
			{ propName, IKIGAI::IMGUI::CombineVecEdit(propName, 4, IKIGAI::IMGUI::CombineVecEdit::MODE::POS) });
	}
	auto val = prop.get(*comp);
	if (IKIGAI::IMGUI::CombineVecEdit::Data.at(propName).draw(val)) {
		prop.set(*comp, val);
	}
}

template <class CLASS, class PTR> void widgetColor4(CLASS* comp, IKIGAI::UTILS::MemberInfo<CLASS, PTR>& prop) {
	const std::string& propName = prop.getName();
	if (!IKIGAI::IMGUI::CombineVecEdit::Data.contains(propName)) {
		IKIGAI::IMGUI::CombineVecEdit::Data.insert(
			{ propName, IKIGAI::IMGUI::CombineVecEdit(propName, 4, IKIGAI::IMGUI::CombineVecEdit::MODE::COLOR) });
	}
	auto val = prop.get(*comp);
	if (IKIGAI::IMGUI::CombineVecEdit::Data.at(propName).draw(val)) {
		prop.set(*comp, val);
	}
}
template <class CLASS, class PTR> void widgetFloat3(CLASS* comp, IKIGAI::UTILS::MemberInfo<CLASS, PTR>& prop) {
	const std::string& propName = prop.getName();
	if (!IKIGAI::IMGUI::CombineVecEdit::Data.contains(propName)) {
		IKIGAI::IMGUI::CombineVecEdit::Data.insert(
			{ propName, IKIGAI::IMGUI::CombineVecEdit(propName, 3, IKIGAI::IMGUI::CombineVecEdit::MODE::POS) });
	}
	auto val = prop.get(*comp);
	if (IKIGAI::IMGUI::CombineVecEdit::Data.at(propName).draw(val)) {
		prop.set(*comp, val);
	}
}

template <class CLASS, class PTR> void widgetColor3(CLASS* comp, IKIGAI::UTILS::MemberInfo<CLASS, PTR>& prop) {
	const std::string& propName = prop.getName();
	if (!IKIGAI::IMGUI::CombineVecEdit::Data.contains(propName)) {
		IKIGAI::IMGUI::CombineVecEdit::Data.insert(
			{ propName, IKIGAI::IMGUI::CombineVecEdit(propName, 3, IKIGAI::IMGUI::CombineVecEdit::MODE::COLOR) });
	}
	auto val = prop.get(*comp);
	if (IKIGAI::IMGUI::CombineVecEdit::Data.at(propName).draw(val)) {
		prop.set(*comp, val);
	}
}

template <class CLASS, class PTR> void widgetFloat(CLASS* comp, IKIGAI::UTILS::MemberInfo<CLASS, PTR>& prop) { }
template <class CLASS> void widgetFloat(CLASS* comp, IKIGAI::UTILS::MemberInfo<CLASS, float>& prop) {
	const std::string& propName = prop.getName();
	float val = prop.get(*comp);

	// const auto flagsRageData = prop.getMetadata().at();
	// const auto flagsStepData = prop.get_metadata();
	auto _speed = 1;
	auto _min = 0.0f;
	auto _max = 0.0f;
	if (prop.getMetadata().contains(IKIGAI::UTILS::MetaParam::EDIT_RANGE)) {
		auto v = std::get<IKIGAI::MATH::Vector2f>(prop.getMetadata().at(IKIGAI::UTILS::MetaParam::EDIT_RANGE));
		_min = v.x;
		_max = v.y;
	}
	if (prop.getMetadata().contains(IKIGAI::UTILS::MetaParam::EDIT_STEP)) {
		_speed = std::get<float>(prop.getMetadata().at(IKIGAI::UTILS::MetaParam::EDIT_STEP));
	}
	if (ImGui::DragFloat(propName.c_str(), &val, _speed, _min, _max)) {
		prop.set(*comp, val);
	}
}

template <class CLASS, class PTR> void widgetInt(CLASS* comp, IKIGAI::UTILS::MemberInfo<CLASS, PTR>& prop) { }
template <class CLASS> void widgetInt(CLASS* comp, IKIGAI::UTILS::MemberInfo<CLASS, int>& prop) {
	const std::string& propName = prop.getName();
	auto val = prop.get(*comp);

	// const auto flagsRageData = prop.getMetadata().at();
	// const auto flagsStepData = prop.get_metadata();
	auto _speed = 1;
	auto _min = 0;
	auto _max = 0;
	if (prop.getMetadata().contains(IKIGAI::UTILS::MetaParam::EDIT_RANGE)) {
		auto v = std::get<IKIGAI::MATH::Vector2f>(prop.getMetadata().at(IKIGAI::UTILS::MetaParam::EDIT_RANGE));
		_min = v.x;
		_max = v.y;
	}
	if (prop.getMetadata().contains(IKIGAI::UTILS::MetaParam::EDIT_STEP)) {
		_speed = std::get<int>(prop.getMetadata().at(IKIGAI::UTILS::MetaParam::EDIT_STEP));
	}
	if (ImGui::DragInt(propName.c_str(), &val, _speed, _min, _max)) {
		prop.set(*comp, val);
	}
}

template <class CLASS, class PTR> void widgetBool(CLASS* comp, IKIGAI::UTILS::MemberInfo<CLASS, PTR>& prop) { }
template <class CLASS> void widgetBool(CLASS* comp, IKIGAI::UTILS::MemberInfo<CLASS, bool>& prop) {
	const std::string& propName = prop.getName();
	auto val = prop.get(*comp);
	if (ImGui::Checkbox(propName.c_str(), &val)) {
		prop.set(*comp, val);
	}
}

template <class CLASS, class PTR> void widgetString(CLASS* comp, IKIGAI::UTILS::MemberInfo<CLASS, PTR>& prop) { }
template <class CLASS> void widgetString(CLASS* comp, IKIGAI::UTILS::MemberInfo<CLASS, const std::string&>& prop) {
	const std::string& propName = prop.getName();
	auto val = prop.get(*comp);
	if (ImGui::InputText(propName.c_str(), &val)) {
		prop.set(*comp, val);
	}
}
template <class CLASS> void widgetString(CLASS* comp, IKIGAI::UTILS::MemberInfo<CLASS, std::string>& prop) {
	const std::string& propName = prop.getName();
	auto val = prop.get(*comp);
	if (ImGui::InputText(propName.c_str(), &val)) {
		prop.set(*comp, val);
	}
}
template <class CLASS, class PTR, typename std::enable_if<!std::is_enum<PTR>::value>::type* = nullptr>
void widgetCombo(CLASS* comp, IKIGAI::UTILS::MemberInfo<CLASS, PTR>& prop) { }

template <class CLASS, class PTR, typename std::enable_if<std::is_enum<PTR>::value>::type* = nullptr>
void widgetCombo(CLASS* comp, IKIGAI::UTILS::MemberInfo<CLASS, PTR>& prop) {
	// TODO: support my enums
	const std::string propName = prop.getName();

	auto val = prop.get(*comp);
	constexpr auto enumNames = magic_enum::enum_names<PTR>();
	constexpr auto enumValues = magic_enum::enum_values<PTR>();

	int itemCurrentIndex = [&enumValues, &val]() {
		int i = 0;
		for (auto& e : enumValues) {
			if (e == val) {
				break;
			}
			i++;
		}
		return i;
	}();
	const auto comboLabel = enumNames[itemCurrentIndex];
	if (ImGui::BeginCombo(propName.c_str(), comboLabel.data())) {
		for (int n = 0; n < enumNames.size(); n++) {
			const bool isSelected = (itemCurrentIndex == n);
			if (ImGui::Selectable(enumNames[n].data(), isSelected)) {
				itemCurrentIndex = n;
				prop.set(*comp, enumValues[itemCurrentIndex]);
			}
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
}

template <typename T> void getPropsImpl(IKIGAI::UTILS::WeakPtr<IKIGAI::ECS::ComponentBase> comp) {
	if (comp->getName() == IKIGAI::ECS::GetComponentName<T>()) {
		auto props = T::GetMembers();
		std::apply(
			[&comp]<typename... Args>(Args&... tpl) {
				auto drawElem = [&comp](auto& prop) {
					const auto propName = prop.getName();
					const auto wType = std::get<IKIGAI::UTILS::WidgetType>(prop.getMetadata().at(IKIGAI::UTILS::MetaParam::EDIT_WIDGET));
					switch (wType) {
					case IKIGAI::UTILS::WidgetType::DRAG_FLOAT_4: {
						widgetFloat4(static_cast<T*>(comp.get()), prop);
						break;
					}
					case IKIGAI::UTILS::WidgetType::DRAG_COLOR_4: {
						widgetColor4(static_cast<T*>(comp.get()), prop);
						break;
					}
					case IKIGAI::UTILS::WidgetType::DRAG_FLOAT_3: {
						widgetFloat3(static_cast<T*>(comp.get()), prop);
						break;
					}
					case IKIGAI::UTILS::WidgetType::DRAG_COLOR_3: {
						widgetColor3(static_cast<T*>(comp.get()), prop);
						break;
					}
					case IKIGAI::UTILS::WidgetType::DRAG_FLOAT: {
						widgetFloat(static_cast<T*>(comp.get()), prop);
						break;
					}
					case IKIGAI::UTILS::WidgetType::DRAG_INT: {
						widgetInt(static_cast<T*>(comp.get()), prop);
						break;
					}
					case IKIGAI::UTILS::WidgetType::BOOL: {
						widgetBool(static_cast<T*>(comp.get()), prop);
						break;
					}
					case IKIGAI::UTILS::WidgetType::STRING: {
						widgetString(static_cast<T*>(comp.get()), prop);
						break;
					}
					case IKIGAI::UTILS::WidgetType::COMBO: {
						widgetCombo(static_cast<T*>(comp.get()), prop);
						break;
					}
					}
				};
				(drawElem(tpl), ...);
			},
			props);
	}
}

template <typename T>
void getPropsImpl(const std::string& name, const IKIGAI::ECS::Object::Id& id, IKIGAI::UTILS::WeakPtr<IKIGAI::ECS::ComponentBase> comp) {
	using namespace IKIGAI;
	const auto componentName = comp->getName();
	if (componentName != ECS::GetComponentName<T>()) {
		return;
	}
	auto& world = comp->obj->getWorld();
	const auto manager = world.getComponentManagerWeak();
	const auto handle = world.getComponentManager()->getComponentHandle<T>(id);
	auto members = T::GetMembers();
	std::apply(
		[&](auto&... member) {
			auto collect = [&](auto& property) {
				const auto& propertyName = property.getName();
				const auto trackName = name + componentName + propertyName;
				ANIMATION::AnimationProperty animationProperty;
				ANIMATION::PropType defaultValue;
				if (ANIMATION::detail::tryBindTimelineMember(
						manager, handle, property, trackName, ANIMATION::InterpolationType::LINEAR, animationProperty, &defaultValue)) {
					ObjProps[trackName] = Prop { name, id, componentName, propertyName, std::move(animationProperty), std::move(defaultValue) };
				}
			};
			(collect(member), ...);
		},
		members);
}

template <typename T>
void getPropsImpl(
	const std::string& name, const IKIGAI::ECS::Object::Id& id, const std::string& componentName, const std::string& propName, Prop& prop) {
	using namespace IKIGAI;
	if (componentName != ECS::GetComponentName<T>()) {
		return;
	}
	auto& scene = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().getCurrentScene();
	const auto object = scene.findObjectByID(id);
	if (!object) {
		return;
	}
	const auto trackName = name + componentName + propName;
	ANIMATION::AnimationProperty animationProperty;
	ANIMATION::PropType defaultValue;
	if (ANIMATION::detail::bindTimelineFromMembers<T>(
			object->getWorld(), id, propName, trackName, animationProperty, ANIMATION::InterpolationType::CUSTOM, &defaultValue)) {
		prop = Prop { name, id, componentName, propName, std::move(animationProperty), std::move(defaultValue) };
	}
}

namespace IKIGAI::ECS {
template <class T> void BindComponentEditor(ComponentCatalog::Record& record) {
	record.inspect = [](UTILS::WeakPtr<ComponentBase> component) { getPropsImpl<T>(component); };
	record.collectTimeline
		= [](const std::string& name, Object::Id id, UTILS::WeakPtr<ComponentBase> component) { getPropsImpl<T>(name, id, component); };
	record.bindTimeline = [](const std::string& name, Object::Id id, const std::string& componentName, const std::string& propertyName, void* prop) {
		getPropsImpl<T>(name, id, componentName, propertyName, *static_cast<Prop*>(prop));
	};
}
}

#endif
