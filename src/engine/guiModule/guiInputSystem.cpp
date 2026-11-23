#include "guiInputSystem.h"

#include "coreModule/gui/components/guiTextInputComponent.h"
#include "guiGeometry.h"
#include "resourceModule/serviceManager.h"
#include "sceneModule/sceneManager.h"
#include "sceneModule/sceneSystem.h"
#include <algorithm>
#include <cmath>

namespace IKIGAI::ECS {
namespace {
	using namespace INPUT_SYSTEM;
	using Target = UTILS::WeakPtr<InteractionComponent>;
	using GUI::EventType;
	constexpr std::int64_t keyboardPointer = -1;

	struct Candidate {
		Target target;
		std::pair<int, std::int64_t> order;
		GUI::ClipBounds clip;
		MATH::Matrix4f matrix;
		MATH::Vector2f size;
	};

	bool available(const Target& target) {
		return target && target->mEnabled && target->obj->isAlive() && target->obj->getIsActive() && target->obj->getIsVisible();
	}

	bool isLegacy3D(const Object& object) {
		if (auto sprite = object.getComponent<SpriteComponent>(); sprite && sprite->mIs3D) {
			return true;
		}
		if (auto sprite = object.getComponent<SpriteAnimateComponent>(); sprite && sprite->mIs3D) {
			return true;
		}
		if (auto label = object.getComponent<LabelComponent>(); label && label->mIs3D) {
			return true;
		}
		auto spine = object.getComponent<SpineComponent>();
		return spine && spine->mIs3D;
	}

	std::vector<Candidate> collectCandidates(SCENE_SYSTEM::Scene& scene, MATH::Vector2f viewport) {
		std::vector<Candidate> result;
		for (const auto& object : scene.getObjects()) {
			auto target = object->getComponent<InteractionComponent>();
			if (!target) {
				continue;
			}
			target->clearGuiEvents();
			if (!available(target) || isLegacy3D(*object)) {
				continue;
			}
			auto canvas = GUI::canvasGeometry(*object);
			const auto size = target->hitSize();
			if (canvas.worldSpace || size.x <= 0.f || size.y <= 0.f) {
				continue;
			}
			const auto clip = GUI::clipBounds(*object, viewport);
			const auto matrix = canvas.correction * object->getTransform()->getWorldMatrix();
			if (clip.empty() || !GUI::localPoint(matrix, {}).has_value()) {
				continue;
			}
			// A fully clipped item must also drop out of keyboard navigation.
			{
				const auto origin = matrix * MATH::Vector4f(0, 0, 0, 1);
				const auto extentX = matrix * MATH::Vector4f(size.x, 0, 0, 0);
				const auto extentY = matrix * MATH::Vector4f(0, size.y, 0, 0);
				if (origin.x + std::max(0.f, extentX.x) + std::max(0.f, extentY.x) <= clip.minimum.x
					|| origin.y + std::max(0.f, extentX.y) + std::max(0.f, extentY.y) <= clip.minimum.y
					|| origin.x + std::min(0.f, extentX.x) + std::min(0.f, extentY.x) >= clip.maximum.x
					|| origin.y + std::min(0.f, extentX.y) + std::min(0.f, extentY.y) >= clip.maximum.y) {
					continue;
				}
			}
			result.push_back({ target, GUI::drawOrder(*object, canvas), clip, matrix, size });
		}
		std::stable_sort(result.begin(), result.end(), [](const Candidate& a, const Candidate& b) { return a.order < b.order; });
		return result;
	}

	Target hitTest(const std::vector<Candidate>& candidates, MATH::Vector2f position) {
		for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
			if (it->clip.contains(position) && available(it->target)) {
				const auto point = GUI::localPoint(it->matrix, position);
				if (point && point->x >= 0.f && point->x < it->size.x && point->y >= 0.f && point->y < it->size.y) {
					return it->target;
				}
			}
		}
		return {};
	}

	void emit(Target target, EventType type, MATH::Vector2f position, std::int64_t id) {
		if (target) {
			const auto matrix = GUI::canvasGeometry(target->obj.get()).correction * target->obj->getTransform()->getWorldMatrix();
			target->emitGuiEvent({ type, position, GUI::localPoint(matrix, position).value_or(MATH::Vector2f {}), id });
		}
	}

	MATH::Vector2f center(const Target& target) {
		const auto size = target->hitSize();
		const auto matrix = GUI::canvasGeometry(target->obj.get()).correction * target->obj->getTransform()->getWorldMatrix();
		auto point = matrix * MATH::Vector4f(size.x * .5f, size.y * .5f, 0, 1);
		return { point.x, point.y };
	}
}

GuiInputSystem::GuiInputSystem() {
	mName = "GuiInputSystem";
	mPriority = 11;
	mExclusiveBatch = true;
	setPhases({ SystemPhase::Update });
	runAfter("GuiLayoutSystem");
}

GuiInputSystem::~GuiInputSystem() {
	releaseCapture();
}

void GuiInputSystem::releaseCapture() {
	if (mPointer && RESOURCES::ServiceManager::Check<InputManager>()) {
		RESOURCES::ServiceManager::Get<InputManager>().releasePointerCapture(*mPointer);
	}
	mPointer.reset();
	mKey.reset();
	mGamepad.reset();
}

void GuiInputSystem::setHover(Target target, MATH::Vector2f position, std::int64_t id) {
	if (target == mHovered) {
		return;
	}
	emit(mHovered, EventType::HoverOut, position, id);
	mHovered = target;
	emit(mHovered, EventType::HoverIn, position, id);
}

void GuiInputSystem::setFocus(Target target, MATH::Vector2f position, std::int64_t id) {
	if (target == mFocused) {
		return;
	}
	if (mKey || mGamepad) {
		finishPress(true, false);
	}
	emit(mFocused, EventType::FocusOut, position, id);
	mFocused = target;
	emit(mFocused, EventType::FocusIn, position, id);
}

void GuiInputSystem::finishPress(bool cancelled, bool inside) {
	auto target = mPressed;
	const auto id = mPointer.value_or(keyboardPointer);
	const bool activate = !cancelled && inside && available(target) && !mHoldActivated && target->mHoldDuration <= 0.f;
	mPressed = nullptr;
	releaseCapture();
	emit(target, cancelled ? EventType::PressCancel : EventType::PressFinish, mPressPosition, id);
	if (activate && available(target)) {
		emit(target, EventType::Activate, mPressPosition, id);
	}
}

void GuiInputSystem::onPhase(
	SystemPhase phase, World& world, CommandBuffer& cb, std::chrono::duration<double> dt, const SystemPhaseContext* context) {
	if (const auto* active = dynamic_cast<const SCENE_SYSTEM::ScenePhaseContext*>(context); active && phase == SystemPhase::Update) {
		updateScene(active->scene, dt);
	}
	else {
		System::onPhase(phase, world, cb, dt, context);
	}
}

void GuiInputSystem::onUpdate(World& world, CommandBuffer&, std::chrono::duration<double> dt) {
	if (RESOURCES::ServiceManager::Check<SCENE_SYSTEM::SceneManager>()) {
		auto* scene = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().getPresentedScene();
		if (scene && &scene->getWorld() == &world) {
			updateScene(*scene, dt);
		}
	}
}

void GuiInputSystem::updateScene(SCENE_SYSTEM::Scene& scene, std::chrono::duration<double> dt) {
	if (!RESOURCES::ServiceManager::Check<InputManager>() || !RESOURCES::ServiceManager::Check<WINDOW::Window>()) {
		return;
	}
	auto& input = RESOURCES::ServiceManager::Get<InputManager>();
	auto& window = RESOURCES::ServiceManager::Get<WINDOW::Window>();
	const auto display = window.getPixelSize();
	const auto logical = window.getSize();
	const auto candidates = collectCandidates(scene, { float(display.x), float(display.y) });
	auto listed = [&](const Target& target) {
		return available(target) && std::any_of(candidates.begin(), candidates.end(), [&](const Candidate& c) { return c.target == target; });
	};
	const auto capture = input.getUiCapture();
	const bool focused = window.hasFocus() && logical.x && logical.y && display.x && display.y;
	const bool allowKeyboard = focused && !capture.keyboard;
	const bool allowPointer = focused && !capture.mouse;
	const bool lostGamepad = mGamepad && !input.isGamepadExist(*mGamepad);
	if ((mPointer || mKey || mGamepad)
		&& (!listed(mPressed) || (mPointer && !allowPointer) || ((mKey || mGamepad) && !allowKeyboard) || lostGamepad)) {
		finishPress(true, false);
	}
	if (mFocused && (!listed(mFocused) || !mFocused->mFocusable || !focused)) {
		setFocus({}, mPressPosition, keyboardPointer);
	}
	if (!allowPointer) {
		setHover({}, mPressPosition, mPointer.value_or(kMousePointerId));
	}

	bool began = false;
	auto begin = [&](Target target, MATH::Vector2f position, std::int64_t id) {
		mPressed = target;
		mPressPosition = position;
		mHoldActivated = false;
		began = true;
		emit(target, EventType::Press, position, id);
	};
	if (allowPointer) {
		auto toPixels = [&](MATH::Vector2f point) { return MATH::Vector2f { point.x * display.x / logical.x, point.y * display.y / logical.y }; };
		for (const auto& pointer : input.pointerEventsThisFrame()) {
			if (mPointer && *mPointer != pointer.pointerId) {
				continue;
			}
			auto position = toPixels(pointer.position);
			auto hit = hitTest(candidates, position);
			const bool touch = pointer.source == PointerSource::Touch;
			mTouchHover = touch;
			if (pointer.phase != PointerPhase::Cancelled) {
				setHover(hit, position, pointer.pointerId);
			}
			switch (pointer.phase) {
			case PointerPhase::Began:
				if (mPointer || input.dragCapture().hasCapture()) {
					break;
				}
				setFocus(hit && hit->mFocusable ? hit : Target {}, position, pointer.pointerId);
				if (mKey || mGamepad) {
					finishPress(true, false);
				}
				if (available(hit) && input.tryCapturePointer(pointer.pointerId)) {
					mPointer = pointer.pointerId;
					begin(hit, position, pointer.pointerId);
				}
				break;
			case PointerPhase::Moved:
				if (mPointer) {
					mPressPosition = position;
					emit(mPressed, EventType::PressMove, position, pointer.pointerId);
					if (mPressed && hit != mPressed && !mHoldActivated) {
						mPressed->mHoldElapsed = 0.f;
						mPressed->mHoldProgress = 0.f;
					}
				}
				break;
			case PointerPhase::Ended:
			case PointerPhase::Cancelled:
				if (mPointer) {
					mPressPosition = position;
					finishPress(pointer.phase == PointerPhase::Cancelled, hit == mPressed);
				}
				if (touch || pointer.phase == PointerPhase::Cancelled) {
					setHover({}, position, pointer.pointerId);
				}
				break;
			}
		}
		const auto mouseDelta = input.getMouseDelta();
		if (!mPointer && (mouseDelta.x != 0.f || mouseDelta.y != 0.f)) {
			mTouchHover = false;
		}
		if (!mTouchHover) {
			const auto mouse = input.getMousePosition();
			const auto position = toPixels({ float(mouse.x), float(mouse.y) });
			setHover(hitTest(candidates, position), position, kMousePointerId);
		}
		else if (mPointer) {
			setHover(hitTest(candidates, mPressPosition), mPressPosition, *mPointer);
		}
	}
	if (allowKeyboard) {
		using Button = INPUT::Gamepad::GAMEPAD_BUTTON;
		const auto* gamepad = input.getFirstGamepad();
		auto buttonPressed = [&](Button button) { return gamepad && input.wasButtonPressedThisFrame(gamepad->getId(), button); };
		const bool previous = buttonPressed(Button::dpad_left) || buttonPressed(Button::dpad_up);
		const bool next = buttonPressed(Button::dpad_right) || buttonPressed(Button::dpad_down);
		const bool tab = input.wasKeyPressedThisFrame(EKey::KEY_TAB);
		if (input.wasKeyPressedThisFrame(EKey::KEY_ESCAPE) || buttonPressed(Button::btn_b)) {
			if (mPointer || mKey || mGamepad) {
				finishPress(true, false);
			}
			setFocus({}, mPressPosition, keyboardPointer);
		}
		else if (tab || previous || next) {
			std::vector<Target> stops;
			for (const auto& candidate : candidates) {
				if (available(candidate.target) && candidate.target->mFocusable) {
					stops.push_back(candidate.target);
				}
			}
			if (!stops.empty()) {
				const bool reverse = previous || (tab && (input.isKeyPressed(EKey::KEY_LEFT_SHIFT) || input.isKeyPressed(EKey::KEY_RIGHT_SHIFT)));
				auto found = std::find(stops.begin(), stops.end(), mFocused);
				Target next;
				if (reverse) {
					next = found == stops.begin() || found == stops.end() ? stops.back() : *std::prev(found);
				}
				else {
					next = found == stops.end() || std::next(found) == stops.end() ? stops.front() : *std::next(found);
				}
				setFocus(next, center(next), keyboardPointer);
			}
		}
		if (!mPointer && !mKey && !mGamepad && available(mFocused) && mFocused->mKeyboardActivation
			&& !mFocused->obj->getComponent<GuiTextInputComponent>()) {
			for (auto key : { EKey::KEY_ENTER, EKey::KEY_SPACE, EKey::KEY_KP_ENTER }) {
				if (input.wasKeyPressedThisFrame(key)) {
					mKey = key;
					begin(mFocused, center(mFocused), keyboardPointer);
					break;
				}
			}
			if (!mKey && buttonPressed(Button::btn_a)) {
				mGamepad = gamepad->getId();
				begin(mFocused, center(mFocused), keyboardPointer);
			}
		}
		if ((mKey && input.wasKeyReleasedThisFrame(*mKey)) || (mGamepad && input.wasButtonReleasedThisFrame(*mGamepad, Button::btn_a))) {
			finishPress(false, mFocused == mPressed);
		}
	}
	if (!began && available(mPressed)) {
		const bool inside = mKey || mGamepad || mPressed == mHovered;
		if (inside || mPressed->mHoldDuration <= 0.f) {
			mPressed->mHoldElapsed += float(std::max(0., dt.count()));
		}
		else if (!mHoldActivated) {
			mPressed->mHoldElapsed = 0.f;
		}
		if (mPressed->mHoldDuration > 0.f && !mHoldActivated) {
			mPressed->mHoldProgress = std::clamp(mPressed->mHoldElapsed / mPressed->mHoldDuration, 0.f, 1.f);
			if (inside && mPressed->mHoldProgress >= 1.f) {
				mHoldActivated = true;
				emit(mPressed, EventType::Activate, mPressPosition, mPointer.value_or(keyboardPointer));
			}
		}
		if (mPressed) {
			mPressed->mCurEvent = GuiEventType::PRESS_CONTINUE;
			const auto callback = mPressed->mOnPressContinue;
			if (callback) {
				callback();
			}
		}
	}
}

}
