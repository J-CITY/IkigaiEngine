#include "guiWidgetSystem.h"

#include "coreModule/ecs/object.h"
#include "coreModule/gui/components/guiWidgetComponents.h"
#include "coreModule/gui/components/spriteComponent.h"
#include "coreModule/gui/components/vectorComponents.h"
#include "guiEvent.h"
#include "resourceModule/serviceManager.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace IKIGAI::ECS {
namespace {
	std::shared_ptr<Object> attachedChild(const std::weak_ptr<Object>& visual, const Object& parent) {
		auto child = visual.lock();
		return child && child->getParent().get() == &parent ? child : nullptr;
	}

	bool enabled(const Object& object) {
		return object.isAlive() && object.getIsActive() && object.getIsVisible();
	}

	void publishValueChanged(
		CommandBuffer& commands, const UTILS::WeakPtr<InteractionComponent>& interaction, const std::optional<GUI::Event>& cause) {
		if (interaction) {
			auto event = cause.value_or(GUI::Event { .type = GUI::EventType::ValueChanged, .pointerId = -1 });
			event.type = GUI::EventType::ValueChanged;
			// User callbacks may edit the scene. Run them after releasing component ranges.
			commands.addCommand([interaction, event] {
				if (interaction) {
					interaction->emitGuiEvent(event);
				}
			});
		}
	}

	MATH::Vector2f contentSize(const Object& content, MATH::Vector2f size) {
		const auto& components = std::as_const(*content.getWorld().getComponentManager());
		const auto* layout = components.tryGetComponent<LayoutComponent>(content.getID());
		const auto& fallback = layout ? layout->mContentSize : content.getTransform()->getTransform().getLocalSize();
		if (size.x == 0.0f) {
			size.x = fallback.x;
		}
		if (size.y == 0.0f) {
			size.y = fallback.y;
		}
		return size;
	}
}

GuiWidgetSystem::GuiWidgetSystem() {
	mName = "GuiWidgetSystem";
	setPhases({ SystemPhase::Update });
	mPriority = 12;
	mExclusiveBatch = true;
	runAfter("GuiInputSystem");

	const auto* components = RESOURCES::ServiceManager::Get<World>().getComponentManager();
	mReads = { components->getComponentType<ClipComponent>(), components->getComponentType<LayoutComponent>() };
	mWrites = { components->getComponentType<CheckboxComponent>(), components->getComponentType<SliderComponent>(),
		components->getComponentType<ProgressBarComponent>(), components->getComponentType<LoadingSpinnerComponent>(),
		components->getComponentType<ScrollViewComponent>(), components->getComponentType<TransformComponent>(),
		components->getComponentType<VectorShapeComponent>(), components->getComponentType<InteractionComponent>() };
}

void GuiWidgetSystem::onUpdate(World& world, CommandBuffer& commands, std::chrono::duration<double> elapsed) {
	auto& components = *world.getComponentManager();
	for (auto& checkbox : components.getComponentsArray<CheckboxComponent>()) {
		updateCheckbox(checkbox, checkbox.obj, commands);
	}
	for (auto& slider : components.getComponentsArray<SliderComponent>()) {
		updateSlider(slider, slider.obj, commands);
	}
	for (auto& progress : components.getComponentsArray<ProgressBarComponent>()) {
		updateProgressBar(progress, progress.obj, commands);
	}
	for (auto& spinner : components.getComponentsArray<LoadingSpinnerComponent>()) {
		updateSpinner(spinner, spinner.obj, elapsed.count());
	}
	for (auto& scrollView : components.getComponentsArray<ScrollViewComponent>()) {
		updateScrollView(scrollView, scrollView.obj, commands);
	}
}

void GuiWidgetSystem::updateCheckbox(CheckboxComponent& checkbox, Object& object, CommandBuffer& commands) {
	if (!enabled(object)) {
		return;
	}
	auto interaction = object.getComponent<InteractionComponent>();
	std::optional<GUI::Event> cause;
	if (interaction) {
		for (const auto& event : interaction->guiEvents()) {
			if (event.type == GUI::EventType::Activate) {
				checkbox.setChecked(!checkbox.isChecked());
				cause = event;
			}
		}
	}
	if (auto mark = attachedChild(checkbox.mMark, object)) {
		mark->setVisible(checkbox.isChecked());
	}
	if (std::exchange(checkbox.mValueChanged, false)) {
		publishValueChanged(commands, interaction, cause);
	}
}

void GuiWidgetSystem::updateSlider(SliderComponent& slider, Object& object, CommandBuffer& commands) {
	auto interaction = object.getComponent<InteractionComponent>();
	if (!enabled(object) || !interaction) {
		slider.mDragPointer.reset();
		return;
	}
	const bool horizontal = slider.mSettings.Orientation == GuiOrientation::Horizontal;
	const auto size = interaction->hitSize();
	const float extent = horizontal ? size.x : size.y;
	if (!std::isfinite(extent) || extent <= 0.0f) {
		slider.mDragPointer.reset();
		return;
	}
	std::optional<GUI::Event> cause;
	for (const auto& event : interaction->guiEvents()) {
		if (event.pointerId == -1) {
			continue;
		}
		if (event.type == GUI::EventType::Press && !slider.mDragPointer) {
			slider.mDragPointer = event.pointerId;
		}
		if (slider.mDragPointer != event.pointerId) {
			continue;
		}
		if (event.type == GUI::EventType::Press || event.type == GUI::EventType::PressMove || event.type == GUI::EventType::PressFinish) {
			const auto position = horizontal ? event.localPosition.x : extent - event.localPosition.y;
			const float fraction = std::clamp(position / extent, 0.0f, 1.0f);
			const float before = slider.value();
			slider.setValue(std::lerp(slider.mSettings.Min, slider.mSettings.Max, fraction));
			if (slider.value() != before) {
				cause = event;
			}
		}
		if (event.type == GUI::EventType::PressFinish || event.type == GUI::EventType::PressCancel) {
			slider.mDragPointer.reset();
		}
	}
	if (auto thumb = attachedChild(slider.mThumb, object)) {
		auto transform = thumb->getTransform();
		auto position = transform->getLocalPosition();
		(horizontal ? position.x : position.y) = extent * (horizontal ? slider.fraction() : 1.0f - slider.fraction());
		if (position != transform->getLocalPosition()) {
			transform->setLocalPosition(position);
		}
	}
	if (std::exchange(slider.mValueChanged, false)) {
		publishValueChanged(commands, interaction, cause);
	}
}

void GuiWidgetSystem::updateProgressBar(ProgressBarComponent& progress, Object& object, CommandBuffer& commands) {
	if (!enabled(object)) {
		return;
	}
	auto interaction = object.getComponent<InteractionComponent>();
	if (progress.mSettings.HoldSource == GuiHoldSource::Self) {
		progress.setDisplayedValue(interaction ? interaction->mHoldProgress : progress.mSettings.Value);
	}
	else if (progress.mSettings.HoldSource == GuiHoldSource::Parent) {
		auto parent = object.getParent();
		const auto source = parent ? parent->getComponent<InteractionComponent>() : UTILS::WeakPtr<InteractionComponent> {};
		progress.setDisplayedValue(source ? source->mHoldProgress : progress.mSettings.Value);
	}
	if (auto fill = attachedChild(progress.mFill, object)) {
		if (auto shape = fill->getComponent<VectorShapeComponent>()) {
			const float extent = progress.mSettings.Extent * progress.value();
			(progress.mSettings.Orientation == GuiOrientation::Horizontal ? shape->Size.x : shape->Size.y) = extent;
			fill->setVisible(extent > 0.0f);
		}
	}
	if (std::exchange(progress.mValueChanged, false)) {
		publishValueChanged(commands, interaction, std::nullopt);
	}
}

void GuiWidgetSystem::updateSpinner(LoadingSpinnerComponent& spinner, Object& object, double elapsed) {
	if (!enabled(object)) {
		return;
	}
	if (auto shape = object.getComponent<VectorShapeComponent>()) {
		if (spinner.isRunning()) {
			const double turn = spinner.mAngle + spinner.mSettings.DegreesPerSecond * elapsed;
			if (std::isfinite(turn)) {
				spinner.mAngle = static_cast<float>(std::fmod(turn, 360.0));
			}
		}
		constexpr int vectorArcShape = 4;
		shape->Shape = vectorArcShape;
		shape->Filled = false;
		shape->StartAngle = spinner.mAngle;
		shape->EndAngle = spinner.mAngle + spinner.mSettings.ArcDegrees;
	}
}

void GuiWidgetSystem::updateScrollView(ScrollViewComponent& scrollView, Object& object, CommandBuffer& commands) {
	auto content = attachedChild(scrollView.mContent, object);
	const auto& components = std::as_const(*object.getWorld().getComponentManager());
	const auto* viewport = components.tryGetComponent<ClipComponent>(object.getID());
	if (!enabled(object) || !content || !viewport) {
		scrollView.mDragPointer.reset();
		return;
	}
	auto interaction = object.getComponent<InteractionComponent>();
	const auto size = contentSize(*content, scrollView.mSettings.ContentSize);
	const float horizontalRange = scrollView.mSettings.ScrollHorizontal ? std::max(0.0f, size.x - viewport->mWidth) : 0.0f;
	const float verticalRange = scrollView.mSettings.ScrollVertical ? std::max(0.0f, size.y - viewport->mHeight) : 0.0f;
	const auto clampOffset = [horizontalRange, verticalRange](MATH::Vector2f offset) {
		return MATH::Vector2f { std::clamp(offset.x, 0.0f, horizontalRange), std::clamp(offset.y, 0.0f, verticalRange) };
	};
	scrollView.setOffset(clampOffset(scrollView.offset()));
	std::optional<GUI::Event> cause;
	if (interaction) {
		for (const auto& event : interaction->guiEvents()) {
			if (event.pointerId == -1) {
				continue;
			}
			if (event.type == GUI::EventType::Press && !scrollView.mDragPointer) {
				scrollView.mDragPointer = event.pointerId;
				scrollView.mDragStart = event.localPosition;
				scrollView.mDragStartOffset = scrollView.offset();
			}
			if (scrollView.mDragPointer != event.pointerId) {
				continue;
			}
			if (event.type == GUI::EventType::PressMove || event.type == GUI::EventType::PressFinish) {
				const auto drag = scrollView.mDragStart - event.localPosition;
				const auto before = scrollView.offset();
				scrollView.setOffset(clampOffset(scrollView.mDragStartOffset + drag));
				if (scrollView.offset() != before) {
					cause = event;
				}
			}
			if (event.type == GUI::EventType::PressFinish || event.type == GUI::EventType::PressCancel) {
				scrollView.mDragPointer.reset();
			}
		}
	}
	else {
		scrollView.mDragPointer.reset();
	}
	auto transform = content->getTransform();
	const auto& offset = scrollView.mSettings.Offset;
	const MATH::Vector3f position { -offset.x, -offset.y, transform->getLocalPosition().z };
	if (position != transform->getLocalPosition()) {
		transform->setLocalPosition(position);
	}
	if (scrollView.mAppliedOffset != offset) {
		scrollView.mAppliedOffset = offset;
		publishValueChanged(commands, interaction, cause);
	}
}
}
