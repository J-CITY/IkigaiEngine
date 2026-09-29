#pragma once

#include "directionalLight.h"
#include "utilsModule/reflection/reflection_macros.h"

namespace IKIGAI::ECS { class Object; }

namespace IKIGAI::ECS {
	IKI_CLASS(Groups=[Component])
	class AmbientLight: public LightComponent {
		IKI_GENERATED_BODY(AmbientLight)
	public:
		IKI_CLASS(Name=AmbientLight::Descriptor)
		struct Descriptor: public ComponentBase::Descriptor {
			IKI_GENERATED_BODY(AmbientLight::Descriptor)
			IKI_PROPERTY(SEREALIZE(name="AmbientLightType"))
			std::string Type;
			IKI_PROPERTY(SEREALIZE(name="Color", default=MATH::Vector3f(1.0f, 1.0f, 1.0f)))
			MATH::Vector3f Color;
			IKI_PROPERTY(SEREALIZE(name="Size", default=MATH::Vector3f(1.0f, 1.0f, 1.0f)))
			MATH::Vector3f Size;
			IKI_PROPERTY(SEREALIZE(name="Intensity"))
			float Intensity = 0.0f;
		};

		AmbientLight(UTILS::Ref<ECS::Object> _obj);
		AmbientLight(UTILS::Ref<ECS::Object> _obj, const Descriptor& descriptor);
		AmbientLight(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor) :
			AmbientLight(obj, static_cast<const Descriptor&>(descriptor)) {
		};
		MATH::Vector3f getSize() const;
		void setSize(const MATH::Vector3f& val);
		[[nodiscard]] Descriptor getDescriptor() const;
	public:
		IKI_PROPERTY(Name=Color, Type=MATH::Vector3f, Getter=getColor, Setter=setColor, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget=DRAG_COLOR_3)
		IKI_PROPERTY(Name=Intensity, Type=float, Getter=getIntensity, Setter=setIntensity, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditRange=MATH::Vector2f{0.0f, 1.0f}, EditStep=0.1f, EditWidget=DRAG_FLOAT)
		static auto GetMembers() {
			return std::tuple{
				IKIGAI::UTILS::MakeMemberInfoLambda<AmbientLight, const MATH::Vector3f&>("Color",
				[](AmbientLight& obj) -> const MATH::Vector3f& { return obj.getColor(); },
				[](AmbientLight& obj, const MATH::Vector3f& data) { obj.setColor(data); },
				UTILS::Meta_t{
					{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR},
				{UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_COLOR_3},
				}),
				IKIGAI::UTILS::MakeMemberInfoLambda<AmbientLight, float>("Intensity",
				[](AmbientLight& obj) { return obj.getIntensity(); },
				[](AmbientLight& obj, float data) { return obj.setIntensity(data); },
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
	inline std::string IKIGAI::ECS::GetType<AmbientLight>() {
		return "class IKIGAI::ECS::AmbientLight";
	}

	template <>
	inline std::string IKIGAI::ECS::GetComponentName<AmbientLight>() {
		return "AmbientLight";
	}
}

#include "generated/ambientLight.generated.h"
