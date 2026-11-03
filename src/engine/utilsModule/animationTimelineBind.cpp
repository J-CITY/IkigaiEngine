#include "animationTimelineBind.h"

#include "coreModule/ecs/componentCatalog.h"

namespace IKIGAI::ANIMATION {

bool tryBindTimelineProperty(
	ECS::Object::Id id, std::string_view componentName, std::string_view propertyName, AnimationProperty& out, std::string_view trackName) {
	if (componentName == "EVENT") {
		out = AnimationProperty(std::string(trackName), [](PropType) {}, []() -> PropType { return std::string {}; }, InterpolationType::LINEAR);
		out.setTimelineEventTrack(true);
		return true;
	}
	AnimationProperty scratch(std::string(trackName), [](PropType) {}, []() -> PropType { return 0.f; }, InterpolationType::LINEAR);
	if (!ECS::ComponentCatalog::instance().tryBindTimelineProperty(id, componentName, propertyName, scratch)) {
		return false;
	}
	out = std::move(scratch);
	return true;
}

} // namespace IKIGAI::ANIMATION
