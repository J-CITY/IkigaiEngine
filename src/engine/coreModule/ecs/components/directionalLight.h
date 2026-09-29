#pragma once
#include <string>

#include "lightComponent.h"
#include "utilsModule/reflection/reflection_macros.h"
//#include "../../../DiffuseLightRT.h"

namespace IKIGAI::ECS { class Object; }

namespace IKIGAI::ECS {
	IKI_CLASS(Groups=[Component])
	class DirectionalLight : public LightComponent {
		IKI_GENERATED_BODY(DirectionalLight)
	public:
		IKI_CLASS(Name=DirectionalLight::Descriptor)
		struct Descriptor : public ComponentBase::Descriptor {
			IKI_GENERATED_BODY(DirectionalLight::Descriptor)
			IKI_PROPERTY(SEREALIZE(name="DirectionalLightType"))
			std::string Type;
			IKI_PROPERTY(SEREALIZE(name="Color", default=MATH::Vector3f(1.0f, 1.0f, 1.0f)))
			MATH::Vector3f Color;
			IKI_PROPERTY(SEREALIZE(name="Distance"))
			float Distance;
			IKI_PROPERTY(SEREALIZE(name="Intensity"))
			float Intensity;
		};
		constexpr static size_t TextureCount = 3;
		DirectionalLight(UTILS::Ref<ECS::Object> obj);
		DirectionalLight(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor);
		DirectionalLight(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor) :
			DirectionalLight(obj, static_cast<const Descriptor&>(descriptor)) {
		};

		void setDistance(float d) { distance = d; }
		float getDistance() const { return distance; }

		//TODO: Think about it
		float distance = 0.0f;
		float orthoBoxSize = 100.0f;

		//TODO: delete it
		std::array<MATH::Matrix4f, 3> ProjectionMatrices;
		std::array<MATH::Matrix4f, 3> BiasedProjectionMatrices;
		float zNear = 0.0f;
		float zFar = 0.0f;
		MATH::Vector3f Direction = MATH::Vector3f(0.5f, 1.0f, 1.0f);
		std::array<float, TextureCount> Projections = {15.0f, 150.0f, 1500.0f};
		MATH::Matrix4f shadowProjectionMat;
		MATH::Matrix4f lightView;
		MATH::Matrix4f lightSpaceMatrix;
		size_t depthMapTextureID = 0;
		[[nodiscard]] Descriptor getDescriptor() const;
	public:
		IKI_PROPERTY(Name=Color, Type=MATH::Vector3f, Getter=getColor, Setter=setColor, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget=DRAG_COLOR_3)
		IKI_PROPERTY(Name=Intensity, Type=float, Getter=getIntensity, Setter=setIntensity, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditRange=MATH::Vector2f{0.0f, 1.0f}, EditStep=0.1f, EditWidget=DRAG_FLOAT)
		static auto GetMembers() {
			return std::tuple{
				IKIGAI::UTILS::MakeMemberInfoLambda<DirectionalLight, const MATH::Vector3f&>("Color",
				[](DirectionalLight& obj) -> const MATH::Vector3f& { return obj.getColor(); },
				[](DirectionalLight& obj, const MATH::Vector3f& data) { obj.setColor(data); },
				UTILS::Meta_t{
					{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR},
				{UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_COLOR_3},
				}),
				IKIGAI::UTILS::MakeMemberInfoLambda<DirectionalLight, float>("Intensity",
				[](DirectionalLight& obj) { return obj.getIntensity(); },
				[](DirectionalLight& obj, float data) { return obj.setIntensity(data); },
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
	inline std::string IKIGAI::ECS::GetType<DirectionalLight>() {
		return "class IKIGAI::ECS::DirectionalLight";
	}

	template <>
	inline std::string IKIGAI::ECS::GetComponentName<DirectionalLight>() {
		return "DirectionalLight";
	}
}

#include "generated/directionalLight.generated.h"
