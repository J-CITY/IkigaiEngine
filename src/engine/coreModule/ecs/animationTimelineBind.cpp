#include "animationTimelineBind.h"

#include "componentCatalog.h"
#include "components/transform.h"
#include "sceneModule/sceneManager.h"
#include "utilsModule/log/loggerDefine.h"
#include "utilsModule/serviceManager.h"

namespace IKIGAI::ECS {
using ANIMATION::AnimationProperty;
using ANIMATION::InterpolationType;
using ANIMATION::PropType;
using ANIMATION::TimelineAnimationTrackDescriptor;

ANIMATION::TimelineTrackBinder makeTimelineTrackBinder(World& world) {
	return [&world](const TimelineAnimationTrackDescriptor& track, std::string_view trackName, AnimationProperty& out) {
		// Event tracks have no target component and can carry the unset object ID.
		if (track.componentName == "EVENT") {
			out = AnimationProperty(std::string(trackName), [](PropType) {}, []() -> PropType { return std::string {}; }, InterpolationType::LINEAR);
			out.setTimelineEventTrack(true);
			return true;
		}
		if (track.objectId < 0
			|| !ComponentCatalog::instance().tryBindTimelineProperty(
				world, Entity(Entity::ID(track.objectId)), track.componentName, track.propertyName, trackName, out)) {
			return false;
		}
		out.setEulerShortestPath(track.propertyName == "LocalRotation" || track.propertyName == "LocalRotationDeg");
		return true;
	};
}

namespace {
	template <class Setter, class Getter>
	std::optional<AnimationProperty> bindTransformProperty(
		Entity id, std::string_view nodeName, std::string trackName, Setter set, Getter get, InterpolationType interpolation) {
		auto& scene = UTILS::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().getCurrentScene();
		const auto object = scene.findObjectByID(id);
		if (!object) {
			LOG_ERROR << nodeName << ": missing object id " << id.getUniqueId().value();
			return std::nullopt;
		}
		auto& world = object->getWorld();
		const auto& components = *world.getComponentManager();
		const auto handle = components.getComponentHandle<TransformComponent>(id);
		if (!components.resolveComponent(handle)) {
			LOG_ERROR << nodeName << ": object has no TransformComponent";
			return std::nullopt;
		}
		return ANIMATION::detail::makeComponentProperty<MATH::Vector3f>(
			std::move(trackName), world.getComponentManagerWeak(), handle, set, get, interpolation);
	}
}

std::optional<AnimationProperty> bindNodableTransformTrack(Entity id, NodableTransformTrack track, InterpolationType interpolation) {
	switch (track) {
	case NodableTransformTrack::Position:
		return bindTransformProperty(
			id, "NodableAnimationNodePos", "propPos", &TransformComponent::setLocalPosition, &TransformComponent::getLocalPosition, interpolation);
	case NodableTransformTrack::Scale:
		return bindTransformProperty(
			id, "NodableAnimationNodeScale", "propScale", &TransformComponent::setLocalScale, &TransformComponent::getLocalScale, interpolation);
	case NodableTransformTrack::Rotation:
		return bindTransformProperty(id, "NodableAnimationNodeRotate", "propRotate", &TransformComponent::setLocalRotationDeg,
			&TransformComponent::getLocalRotationDeg, interpolation);
	}
	return std::nullopt;
}

} // namespace IKIGAI::ECS
