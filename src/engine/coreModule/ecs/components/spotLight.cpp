#include "spotLight.h"
#include <string>

using namespace IKIGAI;
using namespace IKIGAI::ECS;

SpotLight::SpotLight(UTILS::Ref<ECS::Object> _obj)
	: LightComponent(_obj) {
	__NAME__ = "SpotLight";
	data.type = RENDER::Light::Type::SPOT;
	// bool initFBOFLag3 = dirShadowFBO.setupFrameBuffer(shadowRes, shadowRes, false);
}

SpotLight::SpotLight(UTILS::Ref<ECS::Object> _obj, const Descriptor& _descriptor)
	: SpotLight(_obj) {
	data.color = _descriptor.Color;
	data.intensity = _descriptor.Intensity;
	data.castShadows = _descriptor.CastShadows;
	data.shadowNear = _descriptor.ShadowNear;
	data.shadowFar = _descriptor.ShadowFar;
	data.constant = _descriptor.Constant;
	data.linear = _descriptor.Linear;
	data.quadratic = _descriptor.Quadratic;
	data.cutoff = _descriptor.Cutoff;
	data.outerCutoff = _descriptor.OuterCutoff;
}

float SpotLight::getConstant() const {
	return data.constant;
}

float SpotLight::getLinear() const {
	return data.linear;
}

float SpotLight::getQuadratic() const {
	return data.quadratic;
}

float SpotLight::getCutoff() const {
	return data.cutoff;
}

float SpotLight::getOuterCutoff() const {
	return data.outerCutoff;
}

void SpotLight::setConstant(float constant) {
	data.constant = constant;
}

void SpotLight::setLinear(float linear) {
	data.linear = linear;
}

void SpotLight::setQuadratic(float quadratic) {
	data.quadratic = quadratic;
}

void SpotLight::setCutoff(float cutoff) {
	data.cutoff = cutoff;
}

void SpotLight::setOuterCutoff(float outerCutoff) {
	data.outerCutoff = outerCutoff;
}

SpotLight::Descriptor SpotLight::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<SpotLight>();
	descriptor.Color = getColor();
	descriptor.Intensity = getIntensity();
	descriptor.CastShadows = data.castShadows;
	descriptor.ShadowNear = data.shadowNear;
	descriptor.ShadowFar = data.shadowFar;
	descriptor.Constant = getConstant();
	descriptor.Linear = getLinear();
	descriptor.Quadratic = getQuadratic();
	descriptor.Cutoff = getCutoff();
	descriptor.OuterCutoff = getOuterCutoff();
	return descriptor;
}

// #include <rttr/registration>
//
// RTTR_REGISTRATION
//{
//	rttr::registration::class_<IKIGAI::ECS::SpotLight>("SpotLight")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	)
//	.property("Color", &IKIGAI::ECS::SpotLight::getColor, &IKIGAI::ECS::SpotLight::setColor)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_RANGE, Pair { 0.0f, 10000.0f }),
//		rttr::metadata(EditorMetaInfo::EDIT_STEP, 0.1f),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::DRAG_COLOR_3)
//	)
//	.property("Intensity", &IKIGAI::ECS::SpotLight::getIntensity, &IKIGAI::ECS::SpotLight::setIntensity)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_RANGE, Pair { 0.0f, 1.0f }),
//		rttr::metadata(EditorMetaInfo::EDIT_STEP, 0.1f),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::DRAG_FLOAT)
//	)
//	.property("Linear", &IKIGAI::ECS::SpotLight::getLinear, &IKIGAI::ECS::SpotLight::setLinear)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_RANGE, Pair { 0.0f, 1.0f }),
//		rttr::metadata(EditorMetaInfo::EDIT_STEP, 0.1f),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::DRAG_FLOAT)
//	)
//	.property("Quadratic", &IKIGAI::ECS::SpotLight::getQuadratic, &IKIGAI::ECS::SpotLight::setQuadratic)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_RANGE, Pair { 0.0f, 1.0f }),
//		rttr::metadata(EditorMetaInfo::EDIT_STEP, 0.1f),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::DRAG_FLOAT)
//	)
//	.property("Constant", &IKIGAI::ECS::SpotLight::getConstant, &IKIGAI::ECS::SpotLight::setConstant)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_RANGE, Pair { 0.0f, 1.0f }),
//		rttr::metadata(EditorMetaInfo::EDIT_STEP, 0.1f),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::DRAG_FLOAT)
//	)
//	.property("Cutoff", &IKIGAI::ECS::SpotLight::getCutoff, &IKIGAI::ECS::SpotLight::setCutoff)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_RANGE, Pair { 0.0f, 1.0f }),
//		rttr::metadata(EditorMetaInfo::EDIT_STEP, 0.1f),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::DRAG_FLOAT)
//	)
//	.property("OuterCutoff", &IKIGAI::ECS::SpotLight::getOuterCutoff, &IKIGAI::ECS::SpotLight::setOuterCutoff)
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
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::SpotLight>();
	return true;
}();
}
