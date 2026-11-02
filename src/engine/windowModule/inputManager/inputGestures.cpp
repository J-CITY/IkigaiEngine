#include "inputGestures.h"

#include <cmath>

namespace IKIGAI::INPUT_SYSTEM {

void SwipeRecognizer::configure(float minDistance, float maxDurationSeconds) {
	mMinDistance = minDistance;
	mMaxDuration = maxDurationSeconds;
}

void SwipeRecognizer::beginFrame() {
	mSwipesThisFrame.clear();
}

void SwipeRecognizer::onPointer(const PointerEvent& event, float deltaSeconds) {
	if (event.phase == PointerPhase::Began) {
		mActive[event.pointerId] = ActiveTouch { event.position, 0.0f, true };
		return;
	}

	auto it = mActive.find(event.pointerId);
	if (it == mActive.end() || !it->second.active) {
		return;
	}

	if (event.phase == PointerPhase::Moved) {
		it->second.elapsed += deltaSeconds;
		return;
	}

	if (event.phase == PointerPhase::Ended || event.phase == PointerPhase::Cancelled) {
		const auto delta = event.position - it->second.start;
		const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
		if (event.phase == PointerPhase::Ended && distance >= mMinDistance && it->second.elapsed <= mMaxDuration) {
			SwipeEvent swipe;
			swipe.pointerId = event.pointerId;
			swipe.distance = distance;
			swipe.direction = { delta.x / distance, delta.y / distance };
			mSwipesThisFrame.push_back(swipe);
		}
		mActive.erase(it);
	}
}

bool DragCapture::tryCapture(std::int64_t pointerId) {
	if (mCapturedPointer.has_value()) {
		return mCapturedPointer.value() == pointerId;
	}
	mCapturedPointer = pointerId;
	return true;
}

void DragCapture::release(std::int64_t pointerId) {
	if (mCapturedPointer == pointerId) {
		mCapturedPointer.reset();
	}
}

bool DragCapture::ownsPointer(std::int64_t pointerId) const {
	return mCapturedPointer == pointerId;
}

void VirtualStick::configure(MATH::Vector2f center, float hitRadius, float deadzone) {
	mCenter = center;
	mHitRadius = hitRadius;
	mDeadzone = deadzone;
}

void VirtualStick::reset() {
	mActivePointer.reset();
	mValue = {};
}

bool VirtualStick::onPointer(const PointerEvent& event) {
	const auto distanceToCenter = [&](const MATH::Vector2f& point) {
		const auto delta = point - mCenter;
		return std::sqrt(delta.x * delta.x + delta.y * delta.y);
	};

	if (event.phase == PointerPhase::Began) {
		if (distanceToCenter(event.position) <= mHitRadius) {
			mActivePointer = event.pointerId;
		}
	}

	if (!mActivePointer.has_value() || mActivePointer.value() != event.pointerId) {
		return false;
	}

	if (event.phase == PointerPhase::Ended || event.phase == PointerPhase::Cancelled) {
		reset();
		return true;
	}

	const auto offset = event.position - mCenter;
	const float len = std::sqrt(offset.x * offset.x + offset.y * offset.y);
	if (len <= 0.0f) {
		mValue = {};
		return true;
	}
	const float clamped = std::min(len, mHitRadius);
	mValue = { offset.x / mHitRadius, offset.y / mHitRadius };
	const float magnitude = std::sqrt(mValue.x * mValue.x + mValue.y * mValue.y);
	if (magnitude <= mDeadzone) {
		mValue = {};
	}
	else if (magnitude > 1.0f) {
		mValue.x /= magnitude;
		mValue.y /= magnitude;
	}
	else {
		const float scaled = (magnitude - mDeadzone) / (1.0f - mDeadzone);
		mValue.x = (mValue.x / magnitude) * scaled;
		mValue.y = (mValue.y / magnitude) * scaled;
	}
	return true;
}

}
