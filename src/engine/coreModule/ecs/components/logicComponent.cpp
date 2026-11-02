#include "logicComponent.h"

#include "transform.h"

using namespace IKIGAI;
using namespace IKIGAI::ECS;

// #include <rttr/registration>
//
// RTTR_REGISTRATION
//{
//	rttr::registration::class_<IKIGAI::ECS::LogicComponent>("LogicComponent")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	);
// }

LogicComponent::Descriptor LogicComponent::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<LogicComponent>();
	return descriptor;
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::LogicComponent>();
	return true;
}();
}
