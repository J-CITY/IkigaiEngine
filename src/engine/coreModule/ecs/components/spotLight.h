#pragma once
#include "lightComponent.h"
#include "lightComponent.h"
#include "mathModule/math.h"
#include "utilsModule/reflection/reflection_macros.h"

namespace IKIGAI::ECS { class Object; }

namespace IKIGAI::ECS {
	IKI_CLASS(Groups=[Component])
	class SpotLight : public LightComponent {
		IKI_GENERATED_BODY(SpotLight)
	public:
		IKI_CLASS(Name=SpotLight::Descriptor)
		struct Descriptor : public ComponentBase::Descriptor {
			IKI_GENERATED_BODY(SpotLight::Descriptor)
			IKI_PROPERTY(SEREALIZE(name="SpotLightType"))
			std::string Type = "class IKIGAI::ECS::SpotLight";
			IKI_PROPERTY(SEREALIZE(name="Color", default=MATH::Vector3f(1.0f, 1.0f, 1.0f)))
			MATH::Vector3f Color;
			IKI_PROPERTY(SEREALIZE(name="Intensity"))
			float Intensity = 0.0f;
			IKI_PROPERTY(SEREALIZE(name="Constant"))
			float Constant = 0.0f;
			IKI_PROPERTY(SEREALIZE(name="Linear"))
			float Linear = 0.0f;
			IKI_PROPERTY(SEREALIZE(name="Quadratic"))
			float Quadratic = 0.0f;
			IKI_PROPERTY(SEREALIZE(name="Cutoff"))
			float Cutoff = 0.0f;
			IKI_PROPERTY(SEREALIZE(name="OuterCutoff"))
			float OuterCutoff = 0.0f;
		};
		SpotLight(UTILS::Ref<ECS::Object> obj);
		SpotLight(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor);
		SpotLight(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor) :
			SpotLight(obj, static_cast<const Descriptor&>(descriptor)) {
		};
		float getConstant() const;
		float getLinear() const;
		float getQuadratic() const;
		float getCutoff() const;
		float getOuterCutoff() const;
		void setConstant(float constant);
		void setLinear(float linear);
		void setQuadratic(float quadratic);
		void setCutoff(float cutoff);
		void setOuterCutoff(float outerCutoff);

		float distance = 0.0f;
		float orthoBoxSize = 3000.0f;

		static constexpr float angle = MATH::TO_RADIANS(90.0f);
		float aspect = 1.0f;

		float maxDistance = 1000.0f;

		MATH::Vector3f Direction;
		MATH::Matrix4f projectionMat;
		MATH::Matrix4f BiasedProjectionMatrix;
		MATH::Matrix4f Transform;

		[[nodiscard]] Descriptor getDescriptor() const;
	public:
		IKI_PROPERTY(Name=Color, Type=MATH::Vector3f, Getter=getColor, Setter=setColor, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget=DRAG_COLOR_3)
		IKI_PROPERTY(Name=Intensity, Type=float, Getter=getIntensity, Setter=setIntensity, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditRange=MATH::Vector2f{0.0f, 1.0f}, EditStep=0.1f, EditWidget=DRAG_FLOAT)
		IKI_PROPERTY(Name=Linear, Type=float, Getter=getLinear, Setter=setLinear, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditRange=MATH::Vector2f{0.0f, 1.0f}, EditStep=0.1f, EditWidget=DRAG_FLOAT)
		IKI_PROPERTY(Name=Quadratic, Type=float, Getter=getQuadratic, Setter=setQuadratic, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditRange=MATH::Vector2f{0.0f, 1.0f}, EditStep=0.1f, EditWidget=DRAG_FLOAT)
		IKI_PROPERTY(Name=Constant, Type=float, Getter=getConstant, Setter=setConstant, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditRange=MATH::Vector2f{0.0f, 1.0f}, EditStep=0.1f, EditWidget=DRAG_FLOAT)
		IKI_PROPERTY(Name=Cutoff, Type=float, Getter=getCutoff, Setter=setCutoff, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditRange=MATH::Vector2f{0.0f, 1.0f}, EditStep=0.1f, EditWidget=DRAG_FLOAT)
		IKI_PROPERTY(Name=OuterCutoff, Type=float, Getter=getOuterCutoff, Setter=setOuterCutoff, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditRange=MATH::Vector2f{0.0f, 1.0f}, EditStep=0.1f, EditWidget=DRAG_FLOAT)
		static auto GetMembers() {
			return std::tuple{
				IKIGAI::UTILS::MakeMemberInfoLambda<SpotLight, const MATH::Vector3f&>("Color",
				[](SpotLight& obj) -> const MATH::Vector3f& { return obj.getColor(); },
				[](SpotLight& obj, const MATH::Vector3f& data) { obj.setColor(data); },
				UTILS::Meta_t{
					{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR},
				{UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_COLOR_3},
				}),
				IKIGAI::UTILS::MakeMemberInfoLambda<SpotLight, float>("Intensity",
				[](SpotLight& obj) { return obj.getIntensity(); },
				[](SpotLight& obj, float data) { return obj.setIntensity(data); },
				UTILS::Meta_t{
					{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR},
				{UTILS::MetaParam::EDIT_RANGE, MATH::Vector2f{0.0f, 1.0f}},
				{UTILS::MetaParam::EDIT_STEP, 0.1f},
				{UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT},
				}),
				IKIGAI::UTILS::MakeMemberInfo("Linear", &SpotLight::getLinear, &SpotLight::setLinear,
				UTILS::Meta_t{
					{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR},
				{UTILS::MetaParam::EDIT_RANGE, MATH::Vector2f{0.0f, 1.0f}},
				{UTILS::MetaParam::EDIT_STEP, 0.1f},
				{UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT},
				}),
				IKIGAI::UTILS::MakeMemberInfo("Quadratic", &SpotLight::getQuadratic, &SpotLight::setQuadratic,
				UTILS::Meta_t{
					{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR},
				{UTILS::MetaParam::EDIT_RANGE, MATH::Vector2f{0.0f, 1.0f}},
				{UTILS::MetaParam::EDIT_STEP, 0.1f},
				{UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT},
				}),
				IKIGAI::UTILS::MakeMemberInfo("Constant", &SpotLight::getConstant, &SpotLight::setConstant,
				UTILS::Meta_t{
					{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR},
				{UTILS::MetaParam::EDIT_RANGE, MATH::Vector2f{0.0f, 1.0f}},
				{UTILS::MetaParam::EDIT_STEP, 0.1f},
				{UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT},
				}),
				IKIGAI::UTILS::MakeMemberInfo("Cutoff", &SpotLight::getCutoff, &SpotLight::setCutoff,
				UTILS::Meta_t{
					{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR},
				{UTILS::MetaParam::EDIT_RANGE, MATH::Vector2f{0.0f, 1.0f}},
				{UTILS::MetaParam::EDIT_STEP, 0.1f},
				{UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT},
				}),
				IKIGAI::UTILS::MakeMemberInfo("OuterCutoff", &SpotLight::getOuterCutoff, &SpotLight::setOuterCutoff,
				UTILS::Meta_t{
					{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR},
				{UTILS::MetaParam::EDIT_RANGE, MATH::Vector2f{0.0f, 1.0f}},
				{UTILS::MetaParam::EDIT_STEP, 0.1f},
				{UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT},
				})
			};
		}
	};

	template <>
	inline std::string ECS::GetType<SpotLight>() {
		return "class IKIGAI::ECS::SpotLight";
	}

	template <>
	inline std::string IKIGAI::ECS::GetComponentName<SpotLight>() {
		return "SpotLight";
	}
}

#include "generated/spotLight.generated.h"
