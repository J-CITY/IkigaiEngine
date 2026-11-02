#pragma once

#include <cstdint>
#include <mathModule/math.h>
#include <optional>
#include <unordered_map>
#include <vector>

namespace IKIGAI::INPUT_SYSTEM {

enum class PointerPhase { Began, Moved, Ended, Cancelled };

enum class PointerSource { Mouse, Touch };

struct PointerEvent {
	std::int64_t pointerId = 0;
	PointerPhase phase = PointerPhase::Moved;
	PointerSource source = PointerSource::Mouse;
	MATH::Vector2f position {};
	MATH::Vector2f delta {};
};

struct SwipeEvent {
	std::int64_t pointerId = 0;
	MATH::Vector2f direction {};
	float distance = 0.0f;
};

class SwipeRecognizer {
public:
	void configure(float minDistance, float maxDurationSeconds);
	void onPointer(const PointerEvent& event, float deltaSeconds);
	[[nodiscard]] const std::vector<SwipeEvent>& swipesThisFrame() const {
		return mSwipesThisFrame;
	}
	void beginFrame();

private:
	float mMinDistance = 48.0f;
	float mMaxDuration = 0.35f;
	struct ActiveTouch {
		MATH::Vector2f start {};
		float elapsed = 0.0f;
		bool active = false;
	};
	std::unordered_map<std::int64_t, ActiveTouch> mActive;
	std::vector<SwipeEvent> mSwipesThisFrame;
};

class DragCapture {
public:
	[[nodiscard]] bool hasCapture() const {
		return mCapturedPointer.has_value();
	}
	[[nodiscard]] int capturedPointerId() const {
		return mCapturedPointer.value_or(-1);
	}
	bool tryCapture(std::int64_t pointerId);
	void release(std::int64_t pointerId);
	bool ownsPointer(std::int64_t pointerId) const;

private:
	std::optional<std::int64_t> mCapturedPointer;
};

class VirtualStick {
public:
	void configure(MATH::Vector2f center, float hitRadius, float deadzone);
	void reset();
	bool onPointer(const PointerEvent& event);
	[[nodiscard]] bool isActive() const {
		return mActivePointer.has_value();
	}
	[[nodiscard]] MATH::Vector2f value() const {
		return mValue;
	}

private:
	MATH::Vector2f mCenter {};
	float mHitRadius = 120.0f;
	float mDeadzone = 0.15f;
	std::optional<std::int64_t> mActivePointer;
	MATH::Vector2f mValue {};
};

}
