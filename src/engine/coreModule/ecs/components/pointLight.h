#pragma once
#include "lightComponent.h"
#include "utilsModule/reflection/reflection_macros.h"

namespace IKIGAI::ECS { class Object; }

namespace IKIGAI::ECS{
	IKI_CLASS(Groups=[Component])
	class PointLight : public LightComponent {
		IKI_GENERATED_BODY(PointLight)
	public:
		IKI_CLASS(Name=PointLight::Descriptor)
		struct Descriptor : public ComponentBase::Descriptor {
			IKI_GENERATED_BODY(PointLight::Descriptor)
			IKI_PROPERTY(SEREALIZE(name="PointLightType"))
			std::string Type;
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
		};
		PointLight(UTILS::Ref<ECS::Object> _obj);
		PointLight(UTILS::Ref<ECS::Object> _obj, const Descriptor& _descriptor);
		PointLight(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor) :
			PointLight(obj, static_cast<const Descriptor&>(descriptor)) {
		};
		float getConstant() const;
		float getLinear() const;
		float getQuadratic() const;
		void setConstant(float constant);
		void setLinear(float linear);
		void setQuadratic(float quadratic);

		std::vector<MATH::Vector3f> DirectionTable =
		{
			 MATH::Vector3f::Normalize(MATH::Vector3f(1.0f, 0.0001f, 0.0001f)),
			 MATH::Vector3f::Normalize(MATH::Vector3f(-1.0f, 0.0001f, 0.0001f)),
			 MATH::Vector3f::Normalize(MATH::Vector3f(0.0001f,    1.0f, 0.0001f)),
			 MATH::Vector3f::Normalize(MATH::Vector3f(0.0001f,   -1.0f, 0.0001f)),
			 MATH::Vector3f::Normalize(MATH::Vector3f(0.0001f, 0.0001f,    1.0f)),
			 MATH::Vector3f::Normalize(MATH::Vector3f(0.0001f, 0.0001f,   -1.0f)),
		};

		std::vector<MATH::Vector3f> UpTable =
		{
			 MATH::Vector3f(0.0f, -1.0f,  0.0f),
			 MATH::Vector3f(0.0f, -1.0f,  0.0f),
			 MATH::Vector3f(0.0f,  0.0f,  1.0f),
			 MATH::Vector3f(0.0f,  0.0f, -1.0f),
			 MATH::Vector3f(0.0f, -1.0f,  0.0f),
			 MATH::Vector3f(0.0f, -1.0f,  0.0f),
		};
		
		float Radius = 0.0f;
		MATH::Matrix4f shadowProjectionMat;
		std::array<MATH::Matrix4f, 6> ProjectionMatrices;

		[[nodiscard]] Descriptor getDescriptor() const;

		//std::shared_ptr<RESOURCES::CubeMap> DepthMap;

		int depthMapTextureId = 0;

		float zFar = 0.0f;

	public:
		IKI_PROPERTY(Name=Color, Type=MATH::Vector3f, Getter=getColor, Setter=setColor, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget=DRAG_COLOR_3)
		IKI_PROPERTY(Name=Intensity, Type=float, Getter=getIntensity, Setter=setIntensity, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditRange=MATH::Vector2f{0.0f, 1.0f}, EditStep=0.1f, EditWidget=DRAG_FLOAT)
		IKI_PROPERTY(Name=Linear, Type=float, Getter=getLinear, Setter=setLinear, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditRange=MATH::Vector2f{0.0f, 1.0f}, EditStep=0.1f, EditWidget=DRAG_FLOAT)
		IKI_PROPERTY(Name=Quadratic, Type=float, Getter=getQuadratic, Setter=setQuadratic, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditRange=MATH::Vector2f{0.0f, 1.0f}, EditStep=0.1f, EditWidget=DRAG_FLOAT)
		IKI_PROPERTY(Name=Constant, Type=float, Getter=getConstant, Setter=setConstant, Flags=[USE_IN_EDITOR_COMPONENT_INSPECTOR], EditRange=MATH::Vector2f{0.0f, 1.0f}, EditStep=0.1f, EditWidget=DRAG_FLOAT)
		static auto GetMembers() {
			return std::tuple{
				IKIGAI::UTILS::MakeMemberInfoLambda<PointLight, const MATH::Vector3f&>("Color", 
				[](PointLight& obj) -> const MATH::Vector3f& { return obj.getColor(); }, 
				[](PointLight& obj, const MATH::Vector3f& data) { obj.setColor(data); },
				UTILS::Meta_t{
					{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR},
				{UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_COLOR_3},
				}),
				IKIGAI::UTILS::MakeMemberInfoLambda<PointLight, float>("Intensity",
				[](PointLight& obj){ return obj.getIntensity(); }, 
				[](PointLight& obj, float data) { return obj.setIntensity(data); },
				UTILS::Meta_t{
					{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR},
				{UTILS::MetaParam::EDIT_RANGE, MATH::Vector2f{0.0f, 1.0f}},
				{UTILS::MetaParam::EDIT_STEP, 0.1f},
				{UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT},
				}),
				IKIGAI::UTILS::MakeMemberInfo("Linear", &PointLight::getLinear, &PointLight::setLinear,
				UTILS::Meta_t{
					{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR},
				{UTILS::MetaParam::EDIT_RANGE, MATH::Vector2f{0.0f, 1.0f}},
				{UTILS::MetaParam::EDIT_STEP, 0.1f},
				{UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT},
				}),
				IKIGAI::UTILS::MakeMemberInfo("Quadratic", &PointLight::getQuadratic, &PointLight::setQuadratic,
				UTILS::Meta_t{
					{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR},
				{UTILS::MetaParam::EDIT_RANGE, MATH::Vector2f{0.0f, 1.0f}},
				{UTILS::MetaParam::EDIT_STEP, 0.1f},
				{UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT},
				}),
				IKIGAI::UTILS::MakeMemberInfo("Constant", &PointLight::getConstant, &PointLight::setConstant,
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
	inline std::string ECS::GetType<PointLight>() {
		return "class IKIGAI::ECS::PointLight";
	}

	template <>
	inline std::string IKIGAI::ECS::GetComponentName<PointLight>() {
		return "PointLight";
	}
}

#include "generated/pointLight.generated.h"
