#pragma once
#include "../../ecs/components/component.h"
#include "mathModule/math.h"
#include "reflectionModule/reflection_macros.h"

namespace IKIGAI::ECS {
IKI_CLASS(Groups = [Component])
class SdfTextComponent : public ComponentBase {
	IKI_GENERATED_BODY(SdfTextComponent)
public:
	explicit SdfTextComponent(UTILS::Ref<Object> obj);
	struct Descriptor : public ComponentBase::Descriptor {
		std::string Type;
		std::string Text = "SDF text";
		std::string Font = "/fonts/default-sdf.font";
		float Size = 32;
		float WrapWidth = 0;
		int Alignment = 0;
		float LetterSpacing = 0;
		float LineSpacing = 1;
		MATH::Vector4f Color = MATH::Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
		float OutlineWidth = 0;
		MATH::Vector4f OutlineColor = MATH::Vector4f(0.0f, 0.0f, 0.0f, 1.0f);
		MATH::Vector2f ShadowOffset = MATH::Vector2f(0.0f, 0.0f);
		MATH::Vector4f ShadowColor = MATH::Vector4f(0.0f, 0.0f, 0.0f, 0.0f);
		int Order = 0;
		template <class Context> static constexpr auto serde(Context& context, Descriptor& value) {
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "SdfTextComponentType")
				.field(&Descriptor::Text, "Text", serde::attribute::default_ { "SDF text" })
				.field(&Descriptor::Font, "Font", serde::attribute::default_ { "/fonts/default-sdf.font" })
				.field(&Descriptor::Size, "Size", serde::attribute::default_ { 32 })
				.field(&Descriptor::WrapWidth, "WrapWidth", serde::attribute::default_ { 0 })
				.field(&Descriptor::Alignment, "Alignment", serde::attribute::default_ { 0 })
				.field(&Descriptor::LetterSpacing, "LetterSpacing", serde::attribute::default_ { 0 })
				.field(&Descriptor::LineSpacing, "LineSpacing", serde::attribute::default_ { 1 })
				.field(&Descriptor::Color, "Color", serde::attribute::default_ { MATH::Vector4f(1.0f, 1.0f, 1.0f, 1.0f) })
				.field(&Descriptor::OutlineWidth, "OutlineWidth", serde::attribute::default_ { 0 })
				.field(&Descriptor::OutlineColor, "OutlineColor", serde::attribute::default_ { MATH::Vector4f(0.0f, 0.0f, 0.0f, 1.0f) })
				.field(&Descriptor::ShadowOffset, "ShadowOffset", serde::attribute::default_ { MATH::Vector2f(0.0f, 0.0f) })
				.field(&Descriptor::ShadowColor, "ShadowColor", serde::attribute::default_ { MATH::Vector4f(0.0f, 0.0f, 0.0f, 0.0f) })
				.field(&Descriptor::Order, "Order", serde::attribute::default_ { 0 });
		}
	};
	SdfTextComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor);
	SdfTextComponent(UTILS::Ref<Object> obj, const ComponentBase::Descriptor& descriptor)
		: SdfTextComponent(obj, static_cast<const Descriptor&>(descriptor)) { }
	Descriptor getDescriptor() const;
	uint64_t configurationHash() const;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = STRING)
	std::string Text = "SDF text";
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = STRING)
	std::string Font = "/fonts/default-sdf.font";
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float Size = 32;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float WrapWidth = 0;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
	int Alignment = 0;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float LetterSpacing = 0;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float LineSpacing = 1;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_COLOR_4)
	MATH::Vector4f Color = MATH::Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float OutlineWidth = 0;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_COLOR_4)
	MATH::Vector4f OutlineColor = MATH::Vector4f(0.0f, 0.0f, 0.0f, 1.0f);
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT_3)
	MATH::Vector2f ShadowOffset = MATH::Vector2f(0.0f, 0.0f);
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_COLOR_4)
	MATH::Vector4f ShadowColor = MATH::Vector4f(0.0f, 0.0f, 0.0f, 0.0f);
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
	int Order = 0;
	static auto GetMembers() {
		return std::tuple { UTILS::MakeMemberInfo("Text", &SdfTextComponent::Text,
								UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
								{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::STRING } }),
			UTILS::MakeMemberInfo("Font", &SdfTextComponent::Font,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::STRING } }),
			UTILS::MakeMemberInfo("Size", &SdfTextComponent::Size,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT } }),
			UTILS::MakeMemberInfo("WrapWidth", &SdfTextComponent::WrapWidth,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT } }),
			UTILS::MakeMemberInfo("Alignment", &SdfTextComponent::Alignment,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_INT } }),
			UTILS::MakeMemberInfo("LetterSpacing", &SdfTextComponent::LetterSpacing,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT } }),
			UTILS::MakeMemberInfo("LineSpacing", &SdfTextComponent::LineSpacing,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT } }),
			UTILS::MakeMemberInfo("Color", &SdfTextComponent::Color,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_COLOR_4 } }),
			UTILS::MakeMemberInfo("OutlineWidth", &SdfTextComponent::OutlineWidth,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT } }),
			UTILS::MakeMemberInfo("OutlineColor", &SdfTextComponent::OutlineColor,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_COLOR_4 } }),
			UTILS::MakeMemberInfo("ShadowOffset", &SdfTextComponent::ShadowOffset,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT_3 } }),
			UTILS::MakeMemberInfo("ShadowColor", &SdfTextComponent::ShadowColor,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_COLOR_4 } }),
			UTILS::MakeMemberInfo("Order", &SdfTextComponent::Order,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_INT } }) };
	}
};
template <> inline std::string GetType<SdfTextComponent>() {
	return "class IKIGAI::ECS::SdfTextComponent";
}
template <> inline std::string GetComponentName<SdfTextComponent>() {
	return "SdfTextComponent";
}

IKI_CLASS(Groups = [Component])
class VectorShapeComponent : public ComponentBase {
	IKI_GENERATED_BODY(VectorShapeComponent)
public:
	explicit VectorShapeComponent(UTILS::Ref<Object> obj);
	struct Descriptor : public ComponentBase::Descriptor {
		std::string Type;
		int Shape = 0;
		MATH::Vector2f Size = MATH::Vector2f(120.0f, 80.0f);
		MATH::Vector2f Radii = MATH::Vector2f(12.0f, 12.0f);
		MATH::Vector4f CornerRadii = MATH::Vector4f(0, 0, 0, 0);
		float StartAngle = 0;
		float EndAngle = 360;
		std::string Points = "M 0 0 L 100 0 L 50 80 Z";
		MATH::Vector4f Color = MATH::Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
		bool Filled = true;
		MATH::Vector4f StrokeColor = MATH::Vector4f(0.1f, 0.1f, 0.1f, 1.0f);
		float StrokeWidth = 0;
		int Cap = 0;
		int Join = 0;
		float MiterLimit = 4;
		bool Gradient = false;
		MATH::Vector4f GradientColor = MATH::Vector4f(0.0f, 0.6f, 1.0f, 1.0f);
		MATH::Vector2f GradientStart = MATH::Vector2f(0.0f, 0.0f);
		MATH::Vector2f GradientEnd = MATH::Vector2f(100.0f, 0.0f);
		int Order = 0;
		template <class Context> static constexpr auto serde(Context& context, Descriptor& value) {
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "VectorShapeComponentType")
				.field(&Descriptor::Shape, "Shape", serde::attribute::default_ { 0 })
				.field(&Descriptor::Size, "Size", serde::attribute::default_ { MATH::Vector2f(120.0f, 80.0f) })
				.field(&Descriptor::Radii, "Radii", serde::attribute::default_ { MATH::Vector2f(12.0f, 12.0f) })
				.field(&Descriptor::CornerRadii, "CornerRadii", serde::attribute::default_ { MATH::Vector4f(0, 0, 0, 0) })
				.field(&Descriptor::StartAngle, "StartAngle", serde::attribute::default_ { 0 })
				.field(&Descriptor::EndAngle, "EndAngle", serde::attribute::default_ { 360 })
				.field(&Descriptor::Points, "Points", serde::attribute::default_ { "M 0 0 L 100 0 L 50 80 Z" })
				.field(&Descriptor::Color, "Color", serde::attribute::default_ { MATH::Vector4f(1.0f, 1.0f, 1.0f, 1.0f) })
				.field(&Descriptor::Filled, "Filled", serde::attribute::default_ { true })
				.field(&Descriptor::StrokeColor, "StrokeColor", serde::attribute::default_ { MATH::Vector4f(0.1f, 0.1f, 0.1f, 1.0f) })
				.field(&Descriptor::StrokeWidth, "StrokeWidth", serde::attribute::default_ { 0 })
				.field(&Descriptor::Cap, "Cap", serde::attribute::default_ { 0 })
				.field(&Descriptor::Join, "Join", serde::attribute::default_ { 0 })
				.field(&Descriptor::MiterLimit, "MiterLimit", serde::attribute::default_ { 4 })
				.field(&Descriptor::Gradient, "Gradient", serde::attribute::default_ { false })
				.field(&Descriptor::GradientColor, "GradientColor", serde::attribute::default_ { MATH::Vector4f(0.0f, 0.6f, 1.0f, 1.0f) })
				.field(&Descriptor::GradientStart, "GradientStart", serde::attribute::default_ { MATH::Vector2f(0.0f, 0.0f) })
				.field(&Descriptor::GradientEnd, "GradientEnd", serde::attribute::default_ { MATH::Vector2f(100.0f, 0.0f) })
				.field(&Descriptor::Order, "Order", serde::attribute::default_ { 0 });
		}
	};
	VectorShapeComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor);
	VectorShapeComponent(UTILS::Ref<Object> obj, const ComponentBase::Descriptor& descriptor)
		: VectorShapeComponent(obj, static_cast<const Descriptor&>(descriptor)) { }
	Descriptor getDescriptor() const;
	uint64_t configurationHash() const;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
	int Shape = 0;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT_3)
	MATH::Vector2f Size = MATH::Vector2f(120.0f, 80.0f);
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT_3)
	MATH::Vector2f Radii = MATH::Vector2f(12.0f, 12.0f);
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT_4)
	MATH::Vector4f CornerRadii = MATH::Vector4f(0, 0, 0, 0);
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float StartAngle = 0;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float EndAngle = 360;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = STRING)
	std::string Points = "M 0 0 L 100 0 L 50 80 Z";
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_COLOR_4)
	MATH::Vector4f Color = MATH::Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = BOOL)
	bool Filled = true;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_COLOR_4)
	MATH::Vector4f StrokeColor = MATH::Vector4f(0.1f, 0.1f, 0.1f, 1.0f);
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float StrokeWidth = 0;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
	int Cap = 0;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
	int Join = 0;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float MiterLimit = 4;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = BOOL)
	bool Gradient = false;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_COLOR_4)
	MATH::Vector4f GradientColor = MATH::Vector4f(0.0f, 0.6f, 1.0f, 1.0f);
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT_3)
	MATH::Vector2f GradientStart = MATH::Vector2f(0.0f, 0.0f);
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT_3)
	MATH::Vector2f GradientEnd = MATH::Vector2f(100.0f, 0.0f);
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
	int Order = 0;
	static auto GetMembers() {
		return std::tuple { UTILS::MakeMemberInfo("Shape", &VectorShapeComponent::Shape,
								UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
								{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_INT } }),
			UTILS::MakeMemberInfo("Size", &VectorShapeComponent::Size,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT_3 } }),
			UTILS::MakeMemberInfo("Radii", &VectorShapeComponent::Radii,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT_3 } }),
			UTILS::MakeMemberInfo("CornerRadii", &VectorShapeComponent::CornerRadii,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT_4 } }),
			UTILS::MakeMemberInfo("StartAngle", &VectorShapeComponent::StartAngle,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT } }),
			UTILS::MakeMemberInfo("EndAngle", &VectorShapeComponent::EndAngle,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT } }),
			UTILS::MakeMemberInfo("Points", &VectorShapeComponent::Points,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::STRING } }),
			UTILS::MakeMemberInfo("Color", &VectorShapeComponent::Color,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_COLOR_4 } }),
			UTILS::MakeMemberInfo("Filled", &VectorShapeComponent::Filled,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::BOOL } }),
			UTILS::MakeMemberInfo("StrokeColor", &VectorShapeComponent::StrokeColor,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_COLOR_4 } }),
			UTILS::MakeMemberInfo("StrokeWidth", &VectorShapeComponent::StrokeWidth,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT } }),
			UTILS::MakeMemberInfo("Cap", &VectorShapeComponent::Cap,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_INT } }),
			UTILS::MakeMemberInfo("Join", &VectorShapeComponent::Join,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_INT } }),
			UTILS::MakeMemberInfo("MiterLimit", &VectorShapeComponent::MiterLimit,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT } }),
			UTILS::MakeMemberInfo("Gradient", &VectorShapeComponent::Gradient,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::BOOL } }),
			UTILS::MakeMemberInfo("GradientColor", &VectorShapeComponent::GradientColor,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_COLOR_4 } }),
			UTILS::MakeMemberInfo("GradientStart", &VectorShapeComponent::GradientStart,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT_3 } }),
			UTILS::MakeMemberInfo("GradientEnd", &VectorShapeComponent::GradientEnd,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT_3 } }),
			UTILS::MakeMemberInfo("Order", &VectorShapeComponent::Order,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_INT } }) };
	}
};
template <> inline std::string GetType<VectorShapeComponent>() {
	return "class IKIGAI::ECS::VectorShapeComponent";
}
template <> inline std::string GetComponentName<VectorShapeComponent>() {
	return "VectorShapeComponent";
}

IKI_CLASS(Groups = [Component])
class VectorPathComponent : public ComponentBase {
	IKI_GENERATED_BODY(VectorPathComponent)
public:
	explicit VectorPathComponent(UTILS::Ref<Object> obj);
	struct Descriptor : public ComponentBase::Descriptor {
		std::string Type;
		std::string Commands = "M 0 60 C 30 -40 90 160 140 60";
		std::string PathAsset = "";
		MATH::Vector4f Color = MATH::Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
		bool Filled = false;
		MATH::Vector4f StrokeColor = MATH::Vector4f(0.1f, 0.1f, 0.1f, 1.0f);
		float StrokeWidth = 3;
		int Cap = 0;
		int Join = 0;
		float MiterLimit = 4;
		bool Gradient = false;
		MATH::Vector4f GradientColor = MATH::Vector4f(0.0f, 0.6f, 1.0f, 1.0f);
		MATH::Vector2f GradientStart = MATH::Vector2f(0.0f, 0.0f);
		MATH::Vector2f GradientEnd = MATH::Vector2f(100.0f, 0.0f);
		int Order = 0;
		template <class Context> static constexpr auto serde(Context& context, Descriptor& value) {
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "VectorPathComponentType")
				.field(&Descriptor::Commands, "Commands", serde::attribute::default_ { "M 0 60 C 30 -40 90 160 140 60" })
				.field(&Descriptor::PathAsset, "PathAsset", serde::attribute::default_ { "" })
				.field(&Descriptor::Color, "Color", serde::attribute::default_ { MATH::Vector4f(1.0f, 1.0f, 1.0f, 1.0f) })
				.field(&Descriptor::Filled, "Filled", serde::attribute::default_ { false })
				.field(&Descriptor::StrokeColor, "StrokeColor", serde::attribute::default_ { MATH::Vector4f(0.1f, 0.1f, 0.1f, 1.0f) })
				.field(&Descriptor::StrokeWidth, "StrokeWidth", serde::attribute::default_ { 3 })
				.field(&Descriptor::Cap, "Cap", serde::attribute::default_ { 0 })
				.field(&Descriptor::Join, "Join", serde::attribute::default_ { 0 })
				.field(&Descriptor::MiterLimit, "MiterLimit", serde::attribute::default_ { 4 })
				.field(&Descriptor::Gradient, "Gradient", serde::attribute::default_ { false })
				.field(&Descriptor::GradientColor, "GradientColor", serde::attribute::default_ { MATH::Vector4f(0.0f, 0.6f, 1.0f, 1.0f) })
				.field(&Descriptor::GradientStart, "GradientStart", serde::attribute::default_ { MATH::Vector2f(0.0f, 0.0f) })
				.field(&Descriptor::GradientEnd, "GradientEnd", serde::attribute::default_ { MATH::Vector2f(100.0f, 0.0f) })
				.field(&Descriptor::Order, "Order", serde::attribute::default_ { 0 });
		}
	};
	VectorPathComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor);
	VectorPathComponent(UTILS::Ref<Object> obj, const ComponentBase::Descriptor& descriptor)
		: VectorPathComponent(obj, static_cast<const Descriptor&>(descriptor)) { }
	Descriptor getDescriptor() const;
	uint64_t configurationHash() const;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = STRING)
	std::string Commands = "M 0 60 C 30 -40 90 160 140 60";
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = STRING)
	std::string PathAsset = "";
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_COLOR_4)
	MATH::Vector4f Color = MATH::Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = BOOL)
	bool Filled = false;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_COLOR_4)
	MATH::Vector4f StrokeColor = MATH::Vector4f(0.1f, 0.1f, 0.1f, 1.0f);
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float StrokeWidth = 3;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
	int Cap = 0;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
	int Join = 0;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float MiterLimit = 4;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = BOOL)
	bool Gradient = false;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_COLOR_4)
	MATH::Vector4f GradientColor = MATH::Vector4f(0.0f, 0.6f, 1.0f, 1.0f);
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT_3)
	MATH::Vector2f GradientStart = MATH::Vector2f(0.0f, 0.0f);
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT_3)
	MATH::Vector2f GradientEnd = MATH::Vector2f(100.0f, 0.0f);
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
	int Order = 0;
	static auto GetMembers() {
		return std::tuple { UTILS::MakeMemberInfo("Commands", &VectorPathComponent::Commands,
								UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
								{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::STRING } }),
			UTILS::MakeMemberInfo("PathAsset", &VectorPathComponent::PathAsset,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::STRING } }),
			UTILS::MakeMemberInfo("Color", &VectorPathComponent::Color,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_COLOR_4 } }),
			UTILS::MakeMemberInfo("Filled", &VectorPathComponent::Filled,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::BOOL } }),
			UTILS::MakeMemberInfo("StrokeColor", &VectorPathComponent::StrokeColor,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_COLOR_4 } }),
			UTILS::MakeMemberInfo("StrokeWidth", &VectorPathComponent::StrokeWidth,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT } }),
			UTILS::MakeMemberInfo("Cap", &VectorPathComponent::Cap,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_INT } }),
			UTILS::MakeMemberInfo("Join", &VectorPathComponent::Join,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_INT } }),
			UTILS::MakeMemberInfo("MiterLimit", &VectorPathComponent::MiterLimit,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT } }),
			UTILS::MakeMemberInfo("Gradient", &VectorPathComponent::Gradient,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::BOOL } }),
			UTILS::MakeMemberInfo("GradientColor", &VectorPathComponent::GradientColor,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_COLOR_4 } }),
			UTILS::MakeMemberInfo("GradientStart", &VectorPathComponent::GradientStart,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT_3 } }),
			UTILS::MakeMemberInfo("GradientEnd", &VectorPathComponent::GradientEnd,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT_3 } }),
			UTILS::MakeMemberInfo("Order", &VectorPathComponent::Order,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_INT } }) };
	}
};
template <> inline std::string GetType<VectorPathComponent>() {
	return "class IKIGAI::ECS::VectorPathComponent";
}
template <> inline std::string GetComponentName<VectorPathComponent>() {
	return "VectorPathComponent";
}

IKI_CLASS(Groups = [Component])
class VectorCanvasComponent : public ComponentBase {
	IKI_GENERATED_BODY(VectorCanvasComponent)
public:
	explicit VectorCanvasComponent(UTILS::Ref<Object> obj);
	struct Descriptor : public ComponentBase::Descriptor {
		std::string Type;
		bool WorldSpace = false;
		bool DepthTest = true;
		int SortingLayer = 0;
		int Order = 0;
		float DpiScale = 1;
		template <class Context> static constexpr auto serde(Context& context, Descriptor& value) {
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "VectorCanvasComponentType")
				.field(&Descriptor::WorldSpace, "WorldSpace", serde::attribute::default_ { false })
				.field(&Descriptor::DepthTest, "DepthTest", serde::attribute::default_ { true })
				.field(&Descriptor::SortingLayer, "SortingLayer", serde::attribute::default_ { 0 })
				.field(&Descriptor::Order, "Order", serde::attribute::default_ { 0 })
				.field(&Descriptor::DpiScale, "DpiScale", serde::attribute::default_ { 1 });
		}
	};
	VectorCanvasComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor);
	VectorCanvasComponent(UTILS::Ref<Object> obj, const ComponentBase::Descriptor& descriptor)
		: VectorCanvasComponent(obj, static_cast<const Descriptor&>(descriptor)) { }
	Descriptor getDescriptor() const;
	uint64_t configurationHash() const;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = BOOL)
	bool WorldSpace = false;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = BOOL)
	bool DepthTest = true;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
	int SortingLayer = 0;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
	int Order = 0;
	IKI_PROPERTY(Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float DpiScale = 1;
	static auto GetMembers() {
		return std::tuple { UTILS::MakeMemberInfo("WorldSpace", &VectorCanvasComponent::WorldSpace,
								UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
								{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::BOOL } }),
			UTILS::MakeMemberInfo("DepthTest", &VectorCanvasComponent::DepthTest,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::BOOL } }),
			UTILS::MakeMemberInfo("SortingLayer", &VectorCanvasComponent::SortingLayer,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_INT } }),
			UTILS::MakeMemberInfo("Order", &VectorCanvasComponent::Order,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_INT } }),
			UTILS::MakeMemberInfo("DpiScale", &VectorCanvasComponent::DpiScale,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT } }) };
	}
};
template <> inline std::string GetType<VectorCanvasComponent>() {
	return "class IKIGAI::ECS::VectorCanvasComponent";
}
template <> inline std::string GetComponentName<VectorCanvasComponent>() {
	return "VectorCanvasComponent";
}

}
#include "generated/vectorComponents.generated.h"
