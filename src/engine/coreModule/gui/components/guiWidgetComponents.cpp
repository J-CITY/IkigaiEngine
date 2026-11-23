#include "guiWidgetComponents.h"

#include "coreModule/ecs/componentCatalog.h"
#include "coreModule/ecs/object.h"
#include "spriteComponent.h"
#include "vectorComponents.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace IKIGAI::ECS {
namespace {
	std::shared_ptr<Object> findVisual(Object& owner, const std::string& name, std::string_view role) {
		if (name.empty()) {
			return {};
		}
		std::shared_ptr<Object> result;
		for (const auto& child : owner.getChildren()) {
			if (child && child->getName() == name) {
				if (result) {
					throw std::invalid_argument(owner.getName() + " has multiple " + std::string(role) + " children named " + name);
				}
				result = child;
			}
		}
		if (!result) {
			throw std::invalid_argument(owner.getName() + " requires a direct " + std::string(role) + " child named " + name);
		}
		return result;
	}

	std::shared_ptr<Object> bindVisual(Object& owner, Object& visual, std::string_view role) {
		auto child = findVisual(owner, visual.getName(), role);
		if (child.get() != &visual) {
			throw std::invalid_argument(owner.getName() + " requires a uniquely named direct " + std::string(role) + " child");
		}
		return child;
	}

	void saveVisualName(std::string& name, const std::weak_ptr<Object>& visual, const Object& owner) {
		if (auto child = visual.lock(); child && child->getParent().get() == &owner) {
			name = child->getName();
		}
	}

	template <class Component> void requireComponent(Object& object, std::string_view role) {
		if (!object.getComponent<Component>()) {
			throw std::invalid_argument(object.getName() + " requires " + GetComponentName<Component>() + " for " + std::string(role));
		}
	}

	float finite(float value, const char* error) {
		if (!std::isfinite(value)) {
			throw std::invalid_argument(error);
		}
		return value;
	}

	void validateOrientation(GuiOrientation orientation) {
		if (orientation != GuiOrientation::Horizontal && orientation != GuiOrientation::Vertical) {
			throw std::invalid_argument("Unknown GUI widget Orientation");
		}
	}
}

CheckboxComponent::CheckboxComponent(UTILS::Ref<Object> object)
	: CheckboxComponent(object, Descriptor {}) { }

CheckboxComponent::CheckboxComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: ComponentBase(object)
	, mSettings(static_cast<const Descriptor&>(descriptor)) {
	__NAME__ = GetComponentName<CheckboxComponent>();
	mSettings.Type = GetType<CheckboxComponent>();
}

CheckboxComponent::Descriptor CheckboxComponent::getDescriptor() const {
	auto descriptor = mSettings;
	saveVisualName(descriptor.Mark, mMark, obj);
	return descriptor;
}

bool CheckboxComponent::isChecked() const {
	return mSettings.Checked;
}

void CheckboxComponent::setChecked(bool checked) {
	if (mSettings.Checked != checked) {
		mSettings.Checked = checked;
		mValueChanged = true;
	}
}

void CheckboxComponent::bindMark(Object& child) {
	mMark = bindVisual(obj, child, "checkbox mark");
	mSettings.Mark = child.getName();
}

void CheckboxComponent::resolveVisuals() {
	mMark = findVisual(obj, mSettings.Mark, "checkbox mark");
}

void CheckboxComponent::onAwake() {
	resolveVisuals();
}

void CheckboxComponent::onEnable() {
	if (mMark.expired() && !mSettings.Mark.empty()) {
		resolveVisuals();
	}
}

SliderComponent::SliderComponent(UTILS::Ref<Object> object)
	: SliderComponent(object, Descriptor {}) { }

SliderComponent::SliderComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: ComponentBase(object)
	, mSettings(static_cast<const Descriptor&>(descriptor)) {
	__NAME__ = GetComponentName<SliderComponent>();
	mSettings.Type = GetType<SliderComponent>();
	validateOrientation(mSettings.Orientation);
	if (!std::isfinite(mSettings.Min) || !std::isfinite(mSettings.Max) || mSettings.Min >= mSettings.Max) {
		throw std::invalid_argument("Slider requires finite Min and Max with Min less than Max");
	}
	if (!std::isfinite(mSettings.Step) || mSettings.Step < 0.0f) {
		throw std::invalid_argument("Slider Step must be finite and non-negative");
	}
	setValue(mSettings.Value);
	mValueChanged = false;
}

SliderComponent::Descriptor SliderComponent::getDescriptor() const {
	auto descriptor = mSettings;
	saveVisualName(descriptor.Thumb, mThumb, obj);
	return descriptor;
}

float SliderComponent::value() const {
	return mSettings.Value;
}

float SliderComponent::fraction() const {
	return static_cast<float>((static_cast<double>(mSettings.Value) - mSettings.Min) / (static_cast<double>(mSettings.Max) - mSettings.Min));
}

void SliderComponent::setValue(float value) {
	value = std::clamp(finite(value, "Slider Value must be finite"), mSettings.Min, mSettings.Max);
	if (mSettings.Step > 0.0f && value > mSettings.Min && value < mSettings.Max) {
		const double delta = static_cast<double>(value) - mSettings.Min;
		const double rounded = mSettings.Min + std::round(delta / mSettings.Step) * mSettings.Step;
		value = static_cast<float>(std::clamp(rounded, static_cast<double>(mSettings.Min), static_cast<double>(mSettings.Max)));
	}
	if (value != mSettings.Value) {
		mSettings.Value = value;
		mValueChanged = true;
	}
}

void SliderComponent::bindThumb(Object& child) {
	mThumb = bindVisual(obj, child, "slider thumb");
	mSettings.Thumb = child.getName();
}

void SliderComponent::resolveVisuals() {
	mThumb = findVisual(obj, mSettings.Thumb, "slider thumb");
}

void SliderComponent::onAwake() {
	resolveVisuals();
}

void SliderComponent::onEnable() {
	if (mThumb.expired() && !mSettings.Thumb.empty()) {
		resolveVisuals();
	}
}

ProgressBarComponent::ProgressBarComponent(UTILS::Ref<Object> object)
	: ProgressBarComponent(object, Descriptor {}) { }

ProgressBarComponent::ProgressBarComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: ComponentBase(object)
	, mSettings(static_cast<const Descriptor&>(descriptor)) {
	__NAME__ = GetComponentName<ProgressBarComponent>();
	mSettings.Type = GetType<ProgressBarComponent>();
	validateOrientation(mSettings.Orientation);
	if (!std::isfinite(mSettings.Extent) || mSettings.Extent < 0.0f) {
		throw std::invalid_argument("Progress bar Extent must be finite and non-negative");
	}
	switch (mSettings.HoldSource) {
	case GuiHoldSource::None:
	case GuiHoldSource::Self:
	case GuiHoldSource::Parent:
		break;
	default:
		throw std::invalid_argument("Unknown progress bar HoldSource");
	}
	setValue(mSettings.Value);
	mValue = mSettings.Value;
	mValueChanged = false;
}

ProgressBarComponent::Descriptor ProgressBarComponent::getDescriptor() const {
	auto descriptor = mSettings;
	saveVisualName(descriptor.Fill, mFill, obj);
	return descriptor;
}

float ProgressBarComponent::value() const {
	return mValue;
}

void ProgressBarComponent::setValue(float value) {
	mSettings.Value = std::clamp(finite(value, "Progress bar Value must be finite"), 0.0f, 1.0f);
	if (mSettings.HoldSource == GuiHoldSource::None) {
		setDisplayedValue(mSettings.Value);
	}
}

void ProgressBarComponent::setDisplayedValue(float value) {
	if (mValue != value) {
		mValue = value;
		mValueChanged = true;
	}
}

void ProgressBarComponent::bindFill(Object& child) {
	auto fill = bindVisual(obj, child, "progress bar fill");
	requireComponent<VectorShapeComponent>(*fill, "progress bar fill");
	mFill = fill;
	mSettings.Fill = child.getName();
}

void ProgressBarComponent::resolveVisuals() {
	auto fill = findVisual(obj, mSettings.Fill, "progress bar fill");
	if (fill) {
		requireComponent<VectorShapeComponent>(*fill, "progress bar fill");
	}
	mFill = fill;
}

void ProgressBarComponent::onAwake() {
	resolveVisuals();
}

void ProgressBarComponent::onEnable() {
	if (mFill.expired() && !mSettings.Fill.empty()) {
		resolveVisuals();
	}
}

LoadingSpinnerComponent::LoadingSpinnerComponent(UTILS::Ref<Object> object)
	: LoadingSpinnerComponent(object, Descriptor {}) { }

LoadingSpinnerComponent::LoadingSpinnerComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: ComponentBase(object)
	, mSettings(static_cast<const Descriptor&>(descriptor)) {
	__NAME__ = GetComponentName<LoadingSpinnerComponent>();
	mSettings.Type = GetType<LoadingSpinnerComponent>();
	finite(mSettings.DegreesPerSecond, "Spinner DegreesPerSecond must be finite");
	if (!std::isfinite(mSettings.ArcDegrees) || mSettings.ArcDegrees <= 0.0f || mSettings.ArcDegrees > 360.0f) {
		throw std::invalid_argument("Spinner ArcDegrees must be in (0, 360]");
	}
}

LoadingSpinnerComponent::Descriptor LoadingSpinnerComponent::getDescriptor() const {
	return mSettings;
}

bool LoadingSpinnerComponent::isRunning() const {
	return mSettings.Running;
}

void LoadingSpinnerComponent::setRunning(bool running) {
	mSettings.Running = running;
}

ScrollViewComponent::ScrollViewComponent(UTILS::Ref<Object> object)
	: ScrollViewComponent(object, Descriptor {}) { }

ScrollViewComponent::ScrollViewComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: ComponentBase(object)
	, mSettings(static_cast<const Descriptor&>(descriptor)) {
	__NAME__ = GetComponentName<ScrollViewComponent>();
	mSettings.Type = GetType<ScrollViewComponent>();
	if (!std::isfinite(mSettings.ContentSize.x) || mSettings.ContentSize.x < 0.0f || !std::isfinite(mSettings.ContentSize.y)
		|| mSettings.ContentSize.y < 0.0f) {
		throw std::invalid_argument("Scroll view ContentSize must be finite and non-negative");
	}
	setOffset(mSettings.Offset);
	mAppliedOffset = mSettings.Offset;
}

ScrollViewComponent::Descriptor ScrollViewComponent::getDescriptor() const {
	auto descriptor = mSettings;
	saveVisualName(descriptor.Content, mContent, obj);
	return descriptor;
}

MATH::Vector2f ScrollViewComponent::offset() const {
	return mSettings.Offset;
}

void ScrollViewComponent::setOffset(MATH::Vector2f offset) {
	finite(offset.x, "Scroll view Offset.x must be finite");
	finite(offset.y, "Scroll view Offset.y must be finite");
	mSettings.Offset = { mSettings.ScrollHorizontal ? std::max(0.0f, offset.x) : 0.0f, mSettings.ScrollVertical ? std::max(0.0f, offset.y) : 0.0f };
}

void ScrollViewComponent::bindContent(Object& child) {
	auto content = bindVisual(obj, child, "scroll view content");
	requireComponent<ClipComponent>(obj, "scroll view viewport");
	mContent = content;
	mSettings.Content = child.getName();
}

void ScrollViewComponent::resolveVisuals() {
	auto content = findVisual(obj, mSettings.Content, "scroll view content");
	if (content) {
		requireComponent<ClipComponent>(obj, "scroll view viewport");
	}
	mContent = content;
}

void ScrollViewComponent::onAwake() {
	resolveVisuals();
}

void ScrollViewComponent::onEnable() {
	if (mContent.expired() && !mSettings.Content.empty()) {
		resolveVisuals();
	}
}

namespace {
	[[maybe_unused]] const bool registerWidgets = [] {
		ComponentCatalog::enroll<CheckboxComponent>(StorageType::Sparse, true);
		ComponentCatalog::enroll<SliderComponent>(StorageType::Sparse, true);
		ComponentCatalog::enroll<ProgressBarComponent>(StorageType::Sparse, true);
		ComponentCatalog::enroll<LoadingSpinnerComponent>(StorageType::Sparse, true);
		ComponentCatalog::enroll<ScrollViewComponent>(StorageType::Sparse, true);
		return true;
	}();
}
}
