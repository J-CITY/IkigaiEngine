#pragma once

#include "lightComponent.h"
#include "utilsModule/reflection/reflection_macros.h"

namespace IKIGAI::ECS { class Object; }

namespace IKIGAI::ECS {
	IKI_CLASS(Groups=[Component])
	class AmbientSphereLight : public LightComponent {
		IKI_GENERATED_BODY(AmbientSphereLight)
	public:
		IKI_CLASS(Name=AmbientSphereLight::Descriptor)
		struct Descriptor : public ComponentBase::Descriptor {
			IKI_GENERATED_BODY(AmbientSphereLight::Descriptor)
			IKI_PROPERTY(SEREALIZE(name="AmbientSphereLightType"))
			std::string Type;
			IKI_PROPERTY(SEREALIZE(name="Color", default=MATH::Vector3f(1.0f, 1.0f, 1.0f)))
			MATH::Vector3f Color;
			IKI_PROPERTY(SEREALIZE(name="Radius", default=1.0f))
			float Radius = 0.0f;
			IKI_PROPERTY(SEREALIZE(name="Intensity"))
			float Intensity;
		};
		AmbientSphereLight(UTILS::Ref<ECS::Object> _obj);
		AmbientSphereLight(UTILS::Ref<ECS::Object> _obj, const Descriptor& descriptor);
		AmbientSphereLight(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor) :
			AmbientSphereLight(obj, static_cast<const Descriptor&>(descriptor)) {
		};

		float getRadius() const;
		void setRadius(float p_radius);
		[[nodiscard]] Descriptor getDescriptor() const;
	public:
		IKI_PROPERTY(Name=Color, Type=MATH::Vector3f, Getter=getColor, Setter=setColor, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget=DRAG_COLOR_3)
		IKI_PROPERTY(Name=Intensity, Type=float, Getter=getIntensity, Setter=setIntensity, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditRange=MATH::Vector2f{0.0f, 1.0f}, EditStep=0.1f, EditWidget=DRAG_FLOAT)
		IKI_PROPERTY(Name=Radius, Type=float, Getter=getRadius, Setter=setRadius, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditRange=MATH::Vector2f{0.0f, 10000.0f}, EditStep=0.1f, EditWidget=DRAG_FLOAT)
		static auto GetMembers() {
			return std::tuple{
				IKIGAI::UTILS::MakeMemberInfoLambda<AmbientSphereLight, const MATH::Vector3f&>("Color",
				[](AmbientSphereLight& obj) -> const MATH::Vector3f& { return obj.getColor(); },
				[](AmbientSphereLight& obj, const MATH::Vector3f& data) { obj.setColor(data); },
				UTILS::Meta_t{
					{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR},
				{UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_COLOR_3},
				}),
				IKIGAI::UTILS::MakeMemberInfoLambda<AmbientSphereLight, float>("Intensity",
				[](AmbientSphereLight& obj) { return obj.getIntensity(); },
				[](AmbientSphereLight& obj, float data) { return obj.setIntensity(data); },
				UTILS::Meta_t{
					{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR},
				{UTILS::MetaParam::EDIT_RANGE, MATH::Vector2f{0.0f, 1.0f}},
				{UTILS::MetaParam::EDIT_STEP, 0.1f},
				{UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT},
				}),
				IKIGAI::UTILS::MakeMemberInfo("Radius", &AmbientSphereLight::getRadius, &AmbientSphereLight::setRadius,
				UTILS::Meta_t{
					{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR},
				{UTILS::MetaParam::EDIT_RANGE, MATH::Vector2f{0.0f, 10000.0f}},
				{UTILS::MetaParam::EDIT_STEP, 0.1f},
				{UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT},
				}),
			};
		}
	};

	template <>
	inline std::string IKIGAI::ECS::GetType<AmbientSphereLight>() {
		return "class IKIGAI::ECS::AmbientSphereLight";
	}
	template <>
	inline std::string IKIGAI::ECS::GetComponentName<AmbientSphereLight>() {
		return "AmbientSphereLight";
	}
}

#include "generated/ambientSphereLight.generated.h"
