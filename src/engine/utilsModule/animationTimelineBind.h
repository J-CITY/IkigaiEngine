#pragma once

#include "animation.h"
#include "ecsModule/world.h"

#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace IKIGAI::ANIMATION::detail {

template <class Value, class Component, class Setter, class Getter>
AnimationProperty makeComponentProperty(std::string name, std::weak_ptr<ECS::ComponentManager> manager, ECS::ComponentHandle<Component> handle,
	Setter set, Getter get, InterpolationType interpolation) {
	return AnimationProperty(
		std::move(name),
		[manager, handle, set](PropType value) {
			const auto* typedValue = std::get_if<Value>(&value);
			if (const auto components = manager.lock(); components && typedValue) {
				if (auto* component = components->resolveComponent(handle)) {
					std::invoke(set, *component, *typedValue);
				}
			}
		},
		[manager, handle, get]() -> PropType {
			if (const auto components = manager.lock()) {
				if (auto* component = components->resolveComponent(handle)) {
					return std::invoke(get, *component);
				}
			}
			return Value {};
		},
		interpolation);
}

template <class T, class Member>
bool tryBindTimelineMember(const std::weak_ptr<ECS::ComponentManager>& manager, ECS::ComponentHandle<T> handle, Member& member,
	std::string_view trackName, InterpolationType interpolation, AnimationProperty& out, PropType* defaultValue = nullptr) {
	using Value = decltype(member.getPropType());
	using UTILS::WidgetType;
	constexpr auto widget = []() -> std::optional<WidgetType> {
		if constexpr (std::is_same_v<Value, MATH::Vector4f>) {
			return WidgetType::DRAG_FLOAT_4;
		}
		else if constexpr (std::is_same_v<Value, MATH::Vector3f>) {
			return WidgetType::DRAG_FLOAT_3;
		}
		else if constexpr (std::is_same_v<Value, float>) {
			return WidgetType::DRAG_FLOAT;
		}
		else if constexpr (std::is_same_v<Value, int>) {
			return WidgetType::DRAG_INT;
		}
		else if constexpr (std::is_same_v<Value, bool>) {
			return WidgetType::BOOL;
		}
		else {
			return std::nullopt;
		}
	}();

	if constexpr (widget.has_value()) {
		const auto& metadata = member.getMetadata();
		const auto editWidget = metadata.find(UTILS::MetaParam::EDIT_WIDGET);
		const auto* widgetType = editWidget != metadata.end() ? std::get_if<WidgetType>(&editWidget->second) : nullptr;
		if (widgetType && *widgetType == *widget) {
			out = makeComponentProperty<Value>(
				std::string(trackName.empty() ? std::string_view(member.getName()) : trackName), manager, handle,
				[member](T& component, Value value) { member.set(component, value); }, [member](T& component) { return member.get(component); },
				interpolation);
			if (defaultValue) {
				*defaultValue = Value {};
			}
			return true;
		}
	}
	return false;
}

template <class T>
bool bindTimelineFromMembers(ECS::World& world, ECS::Entity id, std::string_view propertyName, std::string_view trackName, AnimationProperty& out,
	InterpolationType interpolation = InterpolationType::LINEAR, PropType* defaultValue = nullptr) {
	if constexpr (requires { T::GetMembers(); }) {
		const auto& components = *world.getComponentManager();
		const auto handle = components.getComponentHandle<T>(id);
		if (!components.resolveComponent(handle)) {
			return false;
		}

		const auto manager = world.getComponentManagerWeak();
		auto members = T::GetMembers();
		return std::apply(
			[&](auto&... member) {
				return (
					(propertyName == member.getName() && tryBindTimelineMember(manager, handle, member, trackName, interpolation, out, defaultValue))
					|| ...);
			},
			members);
	}
	return false;
}

} // namespace IKIGAI::ANIMATION::detail

namespace IKIGAI::ANIMATION {
bool tryBindTimelineProperty(ECS::World& world, ECS::Entity id, std::string_view componentName, std::string_view propertyName, AnimationProperty& out,
	std::string_view trackName);
}
