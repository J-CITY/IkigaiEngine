#pragma once

#include "animation.h"
#include "coreModule/ecs/object.h"
#include "coreModule/ecs/world.h"
#include "resourceModule/serviceManager.h"
#include "sceneModule/sceneManager.h"
namespace IKIGAI::ANIMATION::detail {

inline bool isEulerRotationProperty(std::string_view propertyName) {
	return propertyName == "LocalRotation" || propertyName == "LocalRotationDeg";
}

template <class T, class Member> bool tryBindMember(ECS::Object::Id id, std::string_view propertyName, Member& member, AnimationProperty& out) {
	if (propertyName != member.getName()) {
		return false;
	}
	const auto wType = std::get<UTILS::WidgetType>(member.getMetadata().at(UTILS::MetaParam::EDIT_WIDGET));
	auto cm = RESOURCES::ServiceManager::Get<ECS::World>().getComponentManager();
	const std::string trackName = out.getName().empty() ? std::string(member.getName()) : out.getName();
	switch (wType) {
	case UTILS::WidgetType::DRAG_FLOAT_4: {
		if constexpr (std::is_same_v<decltype(member.getPropType()), MATH::Vector4f>) {
			out = AnimationProperty(
				trackName,
				[member, id, cm](PropType val) {
					if (auto* component = cm->getComponent<T>(id).get()) {
						member.set(*component, std::get<MATH::Vector4f>(val));
					}
				},
				[member, id, cm]() -> PropType {
					if (auto* component = cm->getComponent<T>(id).get()) {
						return member.get(*component);
					}
					return MATH::Vector4f {};
				},
				InterpolationType::LINEAR);
			return true;
		}
		break;
	}
	case UTILS::WidgetType::DRAG_FLOAT_3: {
		if constexpr (std::is_same_v<decltype(member.getPropType()), MATH::Vector3f>) {
			const bool eulerShortest = isEulerRotationProperty(propertyName);
			out = AnimationProperty(
				trackName,
				[member, id, cm](PropType val) {
					if (auto* component = cm->getComponent<T>(id).get()) {
						member.set(*component, std::get<MATH::Vector3f>(val));
					}
				},
				[member, id, cm]() -> PropType {
					if (auto* component = cm->getComponent<T>(id).get()) {
						return member.get(*component);
					}
					return MATH::Vector3f {};
				},
				InterpolationType::LINEAR);
			out.setEulerShortestPath(eulerShortest);
			return true;
		}
		break;
	}
	case UTILS::WidgetType::DRAG_FLOAT: {
		if constexpr (std::is_same_v<decltype(member.getPropType()), float>) {
			out = AnimationProperty(
				trackName,
				[member, id, cm](PropType val) {
					if (auto* component = cm->getComponent<T>(id).get()) {
						member.set(*component, std::get<float>(val));
					}
				},
				[member, id, cm]() -> PropType {
					if (auto* component = cm->getComponent<T>(id).get()) {
						return member.get(*component);
					}
					return 0.f;
				},
				InterpolationType::LINEAR);
			return true;
		}
		break;
	}
	case UTILS::WidgetType::DRAG_INT: {
		if constexpr (std::is_same_v<decltype(member.getPropType()), int>) {
			out = AnimationProperty(
				trackName,
				[member, id, cm](PropType val) {
					if (auto* component = cm->getComponent<T>(id).get()) {
						member.set(*component, std::get<int>(val));
					}
				},
				[member, id, cm]() -> PropType {
					if (auto* component = cm->getComponent<T>(id).get()) {
						return member.get(*component);
					}
					return 0;
				},
				InterpolationType::LINEAR);
			return true;
		}
		break;
	}
	case UTILS::WidgetType::BOOL: {
		if constexpr (std::is_same_v<decltype(member.getPropType()), bool>) {
			out = AnimationProperty(
				trackName,
				[member, id, cm](PropType val) {
					if (auto* component = cm->getComponent<T>(id).get()) {
						member.set(*component, std::get<bool>(val));
					}
				},
				[member, id, cm]() -> PropType {
					if (auto* component = cm->getComponent<T>(id).get()) {
						return member.get(*component);
					}
					return false;
				},
				InterpolationType::LINEAR);
			return true;
		}
		break;
	}
	default:
		break;
	}
	return false;
}

template <class T> bool bindTimelineFromMembers(ECS::Object::Id id, std::string_view propertyName, AnimationProperty& out) {
	auto props = T::GetMembers();
	bool bound = false;
	std::apply([&](auto&... members) { ((bound = bound || tryBindMember<T>(id, propertyName, members, out)), ...); }, props);
	return bound;
}

} // namespace IKIGAI::ANIMATION::detail

namespace IKIGAI::ANIMATION {
bool tryBindTimelineProperty(
	ECS::Object::Id id, std::string_view componentName, std::string_view propertyName, AnimationProperty& out, std::string_view trackName);
}
