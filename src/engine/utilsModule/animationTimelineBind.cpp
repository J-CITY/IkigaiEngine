#include "animationTimelineBind.h"

#include "coreModule/ecs/componentCatalog.h"

namespace IKIGAI::ANIMATION {

bool tryBindTimelineProperty(ECS::World& world, ECS::Entity id, std::string_view componentName, std::string_view propertyName, AnimationProperty& out,
	std::string_view trackName) {
	if (componentName == "EVENT") {
		out = AnimationProperty(std::string(trackName), [](PropType) {}, []() -> PropType { return std::string {}; }, InterpolationType::LINEAR);
		out.setTimelineEventTrack(true);
		return true;
	}
	if (!ECS::ComponentCatalog::instance().tryBindTimelineProperty(world, id, componentName, propertyName, trackName, out)) {
		return false;
	}
	out.setEulerShortestPath(propertyName == "LocalRotation" || propertyName == "LocalRotationDeg");
	return true;
}

} // namespace IKIGAI::ANIMATION
