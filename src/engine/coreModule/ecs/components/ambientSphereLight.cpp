#include "ambientSphereLight.h"

using namespace IKIGAI::ECS;

AmbientSphereLight::AmbientSphereLight(UTILS::Ref<ECS::Object> _obj)
	: LightComponent(_obj) {
	__NAME__ = "AmbientSphereLight";
	data.type = RENDER::Light::Type::AMBIENT_SPHERE;
	data.color = { 0.1f, 0.1f, 0.1f };
	data.constant = 1.0f;
}

AmbientSphereLight::AmbientSphereLight(UTILS::Ref<ECS::Object> _obj, const Descriptor& descriptor)
	: LightComponent(_obj) {
	__NAME__ = "AmbientSphereLight";
	data.type = RENDER::Light::Type::AMBIENT_SPHERE;
	data.color = descriptor.Color;
	data.constant = descriptor.Radius;
	data.intensity = descriptor.Intensity;
}

float AmbientSphereLight::getRadius() const {
	return data.quadratic;
}

void AmbientSphereLight::setRadius(float r) {
	data.constant = r;
}

AmbientSphereLight::Descriptor AmbientSphereLight::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<AmbientSphereLight>();
	descriptor.Color = getColor();
	descriptor.Radius = getRadius();
	descriptor.Intensity = getIntensity();
	return descriptor;
}
// #include <rttr/registration>
//
// RTTR_REGISTRATION
//{
//	rttr::registration::class_<IKIGAI::ECS::AmbientSphereLight>("AmbientSphereLight")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	)
//	.property("Color", &IKIGAI::ECS::AmbientSphereLight::getColor, &IKIGAI::ECS::AmbientSphereLight::setColor)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_RANGE, Pair { 0.0f, 10000.0f }),
//		rttr::metadata(EditorMetaInfo::EDIT_STEP, 0.1f),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::DRAG_COLOR_3)
//	)
//	.property("Intensity", &IKIGAI::ECS::AmbientSphereLight::getIntensity, &IKIGAI::ECS::AmbientSphereLight::setIntensity)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_RANGE, Pair { 0.0f, 1.0f }),
//		rttr::metadata(EditorMetaInfo::EDIT_STEP, 0.1f),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::DRAG_FLOAT)
//	)
//	.property("Radius", &IKIGAI::ECS::AmbientSphereLight::getRadius, &IKIGAI::ECS::AmbientSphereLight::setRadius)
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
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::AmbientSphereLight>();
	return true;
}();
}
