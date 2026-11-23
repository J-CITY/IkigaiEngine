#pragma once

#include "coreModule/ecs/components/component.h"
#include "guiModule/guiEvent.h"
#include "mathModule/curves.h"

#include <memory>
#include <vector>

namespace IKIGAI::ECS {

enum class GuiAnimationProperty { Scale, Opacity, PositionX, PositionY, RotationZ };
enum class GuiAnimationNodeKind { Tween, Wait, Sequence, Parallel };

struct GuiAnimationKey {
	float Time = 0.15f;
	float Value = 1.0f;
	MATH::CURVES::InterpolationType Easing = MATH::CURVES::InterpolationType::LINEAR;
	MATH::Vector2f Control1 { 1.0f / 3.0f, 1.0f / 3.0f };
	MATH::Vector2f Control2 { 2.0f / 3.0f, 2.0f / 3.0f };

	template <class Context> static constexpr void serde(Context& context, GuiAnimationKey& value) {
		using namespace serde::attribute;
		using Self = GuiAnimationKey;
		serde::serde_struct(context, value)
			.field(&Self::Time, "Time")
			.field(&Self::Value, "Value")
			.field(&Self::Easing, "Easing", default_ { MATH::CURVES::InterpolationType::LINEAR })
			.field(&Self::Control1, "Control1", default_ { MATH::Vector2f(1.0f / 3.0f, 1.0f / 3.0f) })
			.field(&Self::Control2, "Control2", default_ { MATH::Vector2f(2.0f / 3.0f, 2.0f / 3.0f) });
	}
};

// A file contains one root node. Groups order or overlap their children.
struct GuiAnimationNode {
	GuiAnimationNodeKind Kind = GuiAnimationNodeKind::Tween;
	std::string Target {};
	GuiAnimationProperty Property = GuiAnimationProperty::Scale;
	float Time = 0.15f;
	float Value = 1.0f;
	MATH::CURVES::InterpolationType Easing = MATH::CURVES::InterpolationType::LINEAR;
	MATH::Vector2f Control1 { 1.0f / 3.0f, 1.0f / 3.0f };
	MATH::Vector2f Control2 { 2.0f / 3.0f, 2.0f / 3.0f };
	std::vector<GuiAnimationNode> Children {};

	template <class Context> static constexpr void serde(Context& context, GuiAnimationNode& value) {
		using namespace serde::attribute;
		using Self = GuiAnimationNode;
		serde::serde_struct(context, value)
			.field(&Self::Kind, "Kind")
			.field(&Self::Target, "Target", default_ { "" })
			.field(&Self::Property, "Property", default_ { GuiAnimationProperty::Scale })
			.field(&Self::Time, "Time", default_ { 0.15f })
			.field(&Self::Value, "Value", default_ { 1.0f })
			.field(&Self::Easing, "Easing", default_ { MATH::CURVES::InterpolationType::LINEAR })
			.field(&Self::Control1, "Control1", default_ { MATH::Vector2f(1.0f / 3.0f, 1.0f / 3.0f) })
			.field(&Self::Control2, "Control2", default_ { MATH::Vector2f(2.0f / 3.0f, 2.0f / 3.0f) })
			.field(&Self::Children, "Children", default_ { std::vector<GuiAnimationNode> {} });
	}
};

struct GuiAnimationBinding {
	GUI::EventType Event = GUI::EventType::HoverIn;
	// Empty selects the owner, otherwise select a uniquely named direct child.
	std::string Target {};
	GuiAnimationProperty Property = GuiAnimationProperty::Scale;
	std::vector<GuiAnimationKey> Keys {};
	std::string GraphPath {};

	template <class Context> static constexpr void serde(Context& context, GuiAnimationBinding& value) {
		using namespace serde::attribute;
		using Self = GuiAnimationBinding;
		serde::serde_struct(context, value)
			.field(&Self::Event, "Event")
			.field(&Self::Target, "Target", default_ { "" })
			.field(&Self::Property, "Property", default_ { GuiAnimationProperty::Scale })
			.field(&Self::Keys, "Keys", default_ { std::vector<GuiAnimationKey> {} })
			.field(&Self::GraphPath, "GraphPath", default_ { "" });
	}
};

// Throws std::invalid_argument for invalid timings, curve data or conflicting tweens.
void validateGuiAnimationGraph(const GuiAnimationNode& root);

class GuiAnimationProgram;
class GuiAnimationComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type {};
		std::vector<GuiAnimationBinding> Bindings {};

		template <class Context> static constexpr void serde(Context& context, Descriptor& value) {
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "GuiAnimationComponentType")
				.field(&Descriptor::Bindings, "Bindings", serde::attribute::default_ { std::vector<GuiAnimationBinding> {} });
		}
	};

	explicit GuiAnimationComponent(UTILS::Ref<Object> object);
	GuiAnimationComponent(UTILS::Ref<Object> object, const Descriptor& descriptor);
	GuiAnimationComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor);
	GuiAnimationComponent(GuiAnimationComponent&&) noexcept;
	GuiAnimationComponent& operator=(GuiAnimationComponent&&) noexcept;
	~GuiAnimationComponent() override;

	[[nodiscard]] Descriptor getDescriptor() const;
	[[nodiscard]] const std::vector<GuiAnimationBinding>& bindings() const;
	bool setBindings(std::vector<GuiAnimationBinding> bindings);
	[[nodiscard]] const std::string& validationError() const;
	void onEnable() override;
	void onDisable() override;
	void trigger(GUI::EventType event);
	void advance(float seconds);
	static auto GetMembers() {
		return std::tuple {};
	}

private:
	std::vector<GuiAnimationBinding> mBindings {};
	std::unique_ptr<GuiAnimationProgram> mProgram {};
	std::string mValidationError {};
};

template <> inline std::string GetType<GuiAnimationComponent>() {
	return "class IKIGAI::ECS::GuiAnimationComponent";
}
template <> inline std::string GetComponentName<GuiAnimationComponent>() {
	return "GuiAnimationComponent";
}

} // namespace IKIGAI::ECS
