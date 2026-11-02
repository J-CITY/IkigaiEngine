#include "ambientLight.h"

using namespace IKIGAI::ECS;

AmbientLight::AmbientLight(UTILS::Ref<ECS::Object> _obj)
	: LightComponent(_obj) {
	__NAME__ = "AmbientLight";

	data.type = RENDER::Light::Type::AMBIENT_BOX;
	data.color = { 0.1f, 0.1f, 0.1f };
	data.constant = 1.0f;
	data.linear = 1.0f;
	data.quadratic = 1.0f;
}

AmbientLight::AmbientLight(UTILS::Ref<ECS::Object> _obj, const Descriptor& descriptor)
	: LightComponent(_obj) {
	__NAME__ = "AmbientLight";
	data.type = RENDER::Light::Type::AMBIENT_BOX;

	data.color = descriptor.Color;
	data.constant = descriptor.Size.x;
	data.linear = descriptor.Size.y;
	data.quadratic = descriptor.Size.z;
	data.intensity = descriptor.Intensity;
}

IKIGAI::MATH::Vector3f AmbientLight::getSize() const {
	return { data.constant, data.linear, data.quadratic };
}

void AmbientLight::setSize(const MATH::Vector3f& val) {
	data.constant = val.x;
	data.linear = val.y;
	data.quadratic = val.z;
}

AmbientLight::Descriptor AmbientLight::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<AmbientLight>();
	descriptor.Color = getColor();
	descriptor.Intensity = getIntensity();
	descriptor.Size = getSize();
	return descriptor;
}
// #include <rttr/registration>
//
// RTTR_REGISTRATION
//{
//	rttr::registration::class_<IKIGAI::ECS::AmbientLight>("AmbientLight")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	)
//	.property("Color", &IKIGAI::ECS::AmbientLight::getColor, &IKIGAI::ECS::AmbientLight::setColor)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_RANGE, Pair { 0.0f, 10000.0f }),
//		rttr::metadata(EditorMetaInfo::EDIT_STEP, 0.1f),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::DRAG_COLOR_3)
//	)
//	.property("Intensity", &IKIGAI::ECS::AmbientLight::getIntensity, &IKIGAI::ECS::AmbientLight::setIntensity)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_RANGE, Pair { 0.0f, 1.0f }),
//		rttr::metadata(EditorMetaInfo::EDIT_STEP, 0.1f),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::DRAG_FLOAT)
//	);
// }

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::AmbientLight>();
	return true;
}();
}
