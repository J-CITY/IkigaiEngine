#pragma once

#include "coreModule/ecs/components/component.h"
#include "mathModule/math.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace IKIGAI::ECS {
enum class GuiOrientation { Horizontal, Vertical };
enum class GuiHoldSource { None, Self, Parent };

class GuiWidgetSystem;

class CheckboxComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type {};
		bool Checked = false;
		std::string Mark {};

		template <class Context> static auto serde(Context& context, Descriptor& value) {
			using serde::attribute::default_;
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "CheckboxComponentType")
				.field(&Descriptor::Checked, "Checked", default_ { false })
				.field(&Descriptor::Mark, "Mark", default_ { std::string {} });
		}
	};

	explicit CheckboxComponent(UTILS::Ref<Object> object);
	CheckboxComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor);
	[[nodiscard]] Descriptor getDescriptor() const;

	[[nodiscard]] bool isChecked() const;
	void setChecked(bool checked);
	void bindMark(Object& child);
	void resolveVisuals();
	void onAwake() override;
	void onEnable() override;

	static auto GetMembers() {
		return std::tuple { UTILS::MakeMemberInfo("Checked", &CheckboxComponent::isChecked, &CheckboxComponent::setChecked,
			UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
			{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::BOOL } }) };
	}

private:
	friend class GuiWidgetSystem;
	Descriptor mSettings {};
	std::weak_ptr<Object> mMark {};
	bool mValueChanged = false;
};

// The slider moves its thumb origin across the interaction rectangle.
// Vertical sliders increase from bottom to top.
class SliderComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type {};
		float Min = 0.0f;
		float Max = 1.0f;
		float Value = 0.0f;
		float Step = 0.0f;
		GuiOrientation Orientation = GuiOrientation::Horizontal;
		std::string Thumb {};

		template <class Context> static auto serde(Context& context, Descriptor& value) {
			using serde::attribute::default_;
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "SliderComponentType")
				.field(&Descriptor::Min, "Min", default_ { 0.0f })
				.field(&Descriptor::Max, "Max", default_ { 1.0f })
				.field(&Descriptor::Value, "Value", default_ { 0.0f })
				.field(&Descriptor::Step, "Step", default_ { 0.0f })
				.field(&Descriptor::Orientation, "Orientation", default_ { GuiOrientation::Horizontal })
				.field(&Descriptor::Thumb, "Thumb", default_ { std::string {} });
		}
	};

	explicit SliderComponent(UTILS::Ref<Object> object);
	SliderComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor);
	[[nodiscard]] Descriptor getDescriptor() const;

	[[nodiscard]] float value() const;
	[[nodiscard]] float fraction() const;
	void setValue(float value);
	void bindThumb(Object& child);
	void resolveVisuals();
	void onAwake() override;
	void onEnable() override;

	static auto GetMembers() {
		return std::tuple { UTILS::MakeMemberInfo("Value", &SliderComponent::value, &SliderComponent::setValue,
			UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
			{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT } }) };
	}

private:
	friend class GuiWidgetSystem;
	Descriptor mSettings {};
	std::weak_ptr<Object> mThumb {};
	std::optional<std::int64_t> mDragPointer {};
	bool mValueChanged = false;
};

// Fill is a direct child with a VectorShapeComponent. Extent is its full
// width (horizontal) or height (vertical). The cross-axis size stays authored.
class ProgressBarComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type {};
		float Value = 0.0f;
		GuiOrientation Orientation = GuiOrientation::Horizontal;
		float Extent = 100.0f;
		std::string Fill {};
		GuiHoldSource HoldSource = GuiHoldSource::None;

		template <class Context> static auto serde(Context& context, Descriptor& value) {
			using serde::attribute::default_;
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "ProgressBarComponentType")
				.field(&Descriptor::Value, "Value", default_ { 0.0f })
				.field(&Descriptor::Orientation, "Orientation", default_ { GuiOrientation::Horizontal })
				.field(&Descriptor::Extent, "Extent", default_ { 100.0f })
				.field(&Descriptor::Fill, "Fill", default_ { std::string {} })
				.field(&Descriptor::HoldSource, "HoldSource", default_ { GuiHoldSource::None });
		}
	};

	explicit ProgressBarComponent(UTILS::Ref<Object> object);
	ProgressBarComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor);
	[[nodiscard]] Descriptor getDescriptor() const;

	[[nodiscard]] float value() const;
	void setValue(float value);
	void bindFill(Object& child);
	void resolveVisuals();
	void onAwake() override;
	void onEnable() override;

	static auto GetMembers() {
		return std::tuple { UTILS::MakeMemberInfo("Value", &ProgressBarComponent::value, &ProgressBarComponent::setValue,
			UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
			{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT } }) };
	}

private:
	friend class GuiWidgetSystem;
	void setDisplayedValue(float value);
	Descriptor mSettings {};
	std::weak_ptr<Object> mFill {};
	float mValue = 0.0f;
	bool mValueChanged = false;
};

// Uses the same object's VectorShapeComponent with a positive StrokeWidth.
// Pausing preserves the current angle.
class LoadingSpinnerComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type {};
		bool Running = true;
		float DegreesPerSecond = 240.0f;
		float ArcDegrees = 100.0f;

		template <class Context> static auto serde(Context& context, Descriptor& value) {
			using serde::attribute::default_;
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "LoadingSpinnerComponentType")
				.field(&Descriptor::Running, "Running", default_ { true })
				.field(&Descriptor::DegreesPerSecond, "DegreesPerSecond", default_ { 240.0f })
				.field(&Descriptor::ArcDegrees, "ArcDegrees", default_ { 100.0f });
		}
	};

	explicit LoadingSpinnerComponent(UTILS::Ref<Object> object);
	LoadingSpinnerComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor);
	[[nodiscard]] Descriptor getDescriptor() const;

	[[nodiscard]] bool isRunning() const;
	void setRunning(bool running);

	static auto GetMembers() {
		return std::tuple { UTILS::MakeMemberInfo("Running", &LoadingSpinnerComponent::isRunning, &LoadingSpinnerComponent::setRunning,
			UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
			{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::BOOL } }) };
	}

private:
	friend class GuiWidgetSystem;
	Descriptor mSettings {};
	float mAngle = 0.0f;
};

// The viewport gets its bounds from its ClipComponent. Offset translates the
// direct Content child toward the viewport origin. Zero ContentSize axes use
// the content's layout measurement or local size.
class ScrollViewComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type {};
		std::string Content {};
		MATH::Vector2f ContentSize { 0.0f, 0.0f };
		MATH::Vector2f Offset { 0.0f, 0.0f };
		bool ScrollHorizontal = false;
		bool ScrollVertical = true;

		template <class Context> static auto serde(Context& context, Descriptor& value) {
			using serde::attribute::default_;
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "ScrollViewComponentType")
				.field(&Descriptor::Content, "Content", default_ { std::string {} })
				.field(&Descriptor::ContentSize, "ContentSize", default_ { MATH::Vector2f(0.0f, 0.0f) })
				.field(&Descriptor::Offset, "Offset", default_ { MATH::Vector2f(0.0f, 0.0f) })
				.field(&Descriptor::ScrollHorizontal, "ScrollHorizontal", default_ { false })
				.field(&Descriptor::ScrollVertical, "ScrollVertical", default_ { true });
		}
	};

	explicit ScrollViewComponent(UTILS::Ref<Object> object);
	ScrollViewComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor);
	[[nodiscard]] Descriptor getDescriptor() const;

	[[nodiscard]] MATH::Vector2f offset() const;
	void setOffset(MATH::Vector2f offset);
	void bindContent(Object& child);
	void resolveVisuals();
	void onAwake() override;
	void onEnable() override;

	static auto GetMembers() {
		return std::tuple {};
	}

private:
	friend class GuiWidgetSystem;
	Descriptor mSettings {};
	std::weak_ptr<Object> mContent {};
	std::optional<std::int64_t> mDragPointer {};
	MATH::Vector2f mDragStart { 0.0f, 0.0f };
	MATH::Vector2f mDragStartOffset { 0.0f, 0.0f };
	MATH::Vector2f mAppliedOffset { 0.0f, 0.0f };
};

template <> inline std::string GetType<CheckboxComponent>() {
	return "class IKIGAI::ECS::CheckboxComponent";
}
template <> inline std::string GetComponentName<CheckboxComponent>() {
	return "CheckboxComponent";
}
template <> inline std::string GetType<SliderComponent>() {
	return "class IKIGAI::ECS::SliderComponent";
}
template <> inline std::string GetComponentName<SliderComponent>() {
	return "SliderComponent";
}
template <> inline std::string GetType<ProgressBarComponent>() {
	return "class IKIGAI::ECS::ProgressBarComponent";
}
template <> inline std::string GetComponentName<ProgressBarComponent>() {
	return "ProgressBarComponent";
}
template <> inline std::string GetType<LoadingSpinnerComponent>() {
	return "class IKIGAI::ECS::LoadingSpinnerComponent";
}
template <> inline std::string GetComponentName<LoadingSpinnerComponent>() {
	return "LoadingSpinnerComponent";
}
template <> inline std::string GetType<ScrollViewComponent>() {
	return "class IKIGAI::ECS::ScrollViewComponent";
}
template <> inline std::string GetComponentName<ScrollViewComponent>() {
	return "ScrollViewComponent";
}
}
