#include "directionalLight.h"
#include "../object.h"

using namespace IKIGAI::ECS;

DirectionalLight::DirectionalLight(UTILS::Ref<ECS::Object> obj)
	: LightComponent(obj) {
	__NAME__ = "DirectionalLight";
	data.type = RENDER::Light::Type::DIRECTIONAL;
}

DirectionalLight::DirectionalLight(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor)
	: DirectionalLight(obj) {
	data.type = RENDER::Light::Type::DIRECTIONAL;
	data.color = descriptor.Color;
	data.intensity = descriptor.Intensity;
	data.castShadows = descriptor.CastShadows;
	data.shadowNear = descriptor.ShadowNear;
	data.shadowFar = descriptor.ShadowFar;
	distance = descriptor.Distance;
}

DirectionalLight::Descriptor DirectionalLight::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<DirectionalLight>();
	descriptor.Color = getColor();
	descriptor.Distance = getDistance();
	descriptor.Intensity = getIntensity();
	descriptor.CastShadows = data.castShadows;
	descriptor.ShadowNear = data.shadowNear;
	descriptor.ShadowFar = data.shadowFar;
	return descriptor;
}
// #include <rttr/registration>
//
// RTTR_REGISTRATION
//{
//	rttr::registration::class_<IKIGAI::ECS::DirectionalLight>("DirectionalLight")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	)
//	.property("Color", &IKIGAI::ECS::DirectionalLight::getColor, &IKIGAI::ECS::DirectionalLight::setColor)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE| MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_RANGE, Pair { 0.0f, 10000.0f }),
//		rttr::metadata(EditorMetaInfo::EDIT_STEP, 0.1f),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::DRAG_COLOR_3)
//	)
//	.property("Intensity", &IKIGAI::ECS::DirectionalLight::getIntensity, &IKIGAI::ECS::DirectionalLight::setIntensity)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE| MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_RANGE, Pair { 0.0f, 1.0f }),
//		rttr::metadata(EditorMetaInfo::EDIT_STEP, 0.1f),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::DRAG_FLOAT)
//	)
//	.property("Distance", &IKIGAI::ECS::DirectionalLight::getDistance, &IKIGAI::ECS::DirectionalLight::setDistance)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE| MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_RANGE, Pair { 0.0f, 1000.0f }),
//		rttr::metadata(EditorMetaInfo::EDIT_STEP, 0.1f),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::DRAG_FLOAT)
//	);
// }

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::DirectionalLight>();
	return true;
}();
}
