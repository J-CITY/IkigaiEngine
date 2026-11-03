#include "curves.h"
#include <algorithm>
#include <limits>

using namespace IKIGAI::MATH;
using namespace IKIGAI::MATH::CURVES;

Vector2f IKIGAI::MATH::CURVES::evaluateParametric(const Curve& curve, float t) {
	return (1.f - t) * (1.f - t) * (1.f - t) * curve.mLeft + 3.f * t * (1.f - t) * (1.f - t) * curve.mLeftTangent
		+ 3.f * t * t * (1.f - t) * curve.mRightTangent + t * t * t * curve.mRight;
}

EasingFunction IKIGAI::MATH::CURVES::getEasing(const InterpolationType type) {
	static const EasingFunction kTable[] = {
		&FUNC::linearInterpolation,
		&FUNC::quadraticEaseIn,
		&FUNC::quadraticEaseOut,
		&FUNC::quadraticEaseInOut,
		&FUNC::cubicEaseIn,
		&FUNC::cubicEaseOut,
		&FUNC::cubicEaseInOut,
		&FUNC::quarticEaseIn,
		&FUNC::quarticEaseOut,
		&FUNC::quarticEaseInOut,
		&FUNC::quarticEaseIn,
		&FUNC::quarticEaseOut,
		&FUNC::quarticEaseInOut,
		&FUNC::quinticEaseIn,
		&FUNC::quinticEaseOut,
		&FUNC::quinticEaseInOut,
		&FUNC::sineEaseIn,
		&FUNC::sineEaseOut,
		&FUNC::sineEaseInOut,
		&FUNC::circularEaseIn,
		&FUNC::circularEaseOut,
		&FUNC::circularEaseInOut,
		&FUNC::exponentialEaseIn,
		&FUNC::exponentialEaseOut,
		&FUNC::exponentialEaseInOut,
		&FUNC::elasticEaseIn,
		&FUNC::elasticEaseOut,
		&FUNC::elasticEaseInOut,
		&FUNC::backEaseIn,
		&FUNC::backEaseOut,
		&FUNC::backEaseInOut,
		&FUNC::bounceEaseIn,
		&FUNC::bounceEaseOut,
		&FUNC::bounceEaseInOut,
	};
	const auto index = static_cast<std::size_t>(type);
	if (index >= sizeof(kTable) / sizeof(kTable[0]) || type == InterpolationType::CUSTOM) {
		return nullptr;
	}
	return kTable[index];
}

float IKIGAI::MATH::CURVES::evaluateAtTime(const Curve& curve, float time) {
	if (!std::isfinite(time)) {
		throw std::invalid_argument("Non-finite curve time");
	}
	for (const auto& p : { curve.mLeft, curve.mRight, curve.mLeftTangent, curve.mRightTangent }) {
		if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
			throw std::invalid_argument("Non-finite Bezier control");
		}
	}
	if (!(curve.mLeft.x < curve.mRight.x) || curve.mLeftTangent.x < curve.mLeft.x || curve.mLeftTangent.x > curve.mRightTangent.x
		|| curve.mRightTangent.x > curve.mRight.x) {
		throw std::invalid_argument("Bezier X controls must be monotonic");
	}
	if (time <= curve.mLeft.x) {
		return curve.mLeft.y;
	}
	if (time >= curve.mRight.x) {
		return curve.mRight.y;
	}
	// Bounded bisection also handles zero derivatives at either endpoint.
	float lo = 0, hi = 1;
	for (int i = 0; i < 28; ++i) {
		const float mid = (lo + hi) * .5f;
		if (evaluateParametric(curve, mid).x < time) {
			lo = mid;
		}
		else {
			hi = mid;
		}
	}
	return evaluateParametric(curve, (lo + hi) * .5f).y;
}

void IKIGAI::MATH::CURVES::FloatCurve::validate() const {
	if (keys.empty()) {
		throw std::invalid_argument("Curve needs at least one key");
	}
	float previous = -std::numeric_limits<float>::infinity();
	for (const auto& k : keys) {
		if (!std::isfinite(k.time) || !std::isfinite(k.value) || k.time <= previous) {
			throw std::invalid_argument("Curve keys must be finite and strictly ordered");
		}
		if (!std::isfinite(k.outControl.x) || !std::isfinite(k.outControl.y) || !std::isfinite(k.inControl.x) || !std::isfinite(k.inControl.y)) {
			throw std::invalid_argument("Non-finite curve controls");
		}
		if (k.interpolation == SegmentInterpolation::Bezier && (k.outControl.x < 0 || k.outControl.x > k.inControl.x || k.inControl.x > 1)) {
			throw std::invalid_argument("Bezier X controls must be monotonic in [0,1]");
		}
		if (k.interpolation == SegmentInterpolation::Easing && getEasing(k.easing) == nullptr) {
			throw std::invalid_argument("Unknown easing function");
		}
		previous = k.time;
	}
}

float IKIGAI::MATH::CURVES::FloatCurve::evaluate(float time) const {
	if (keys.empty() || !std::isfinite(time)) {
		throw std::invalid_argument("Invalid curve evaluation");
	}
	if (time <= keys.front().time) {
		return keys.front().value;
	}
	if (time >= keys.back().time) {
		return keys.back().value;
	}
	const auto next = std::upper_bound(keys.begin(), keys.end(), time, [](float t, const FloatKey& k) { return t < k.time; });
	const auto& b = *next;
	const auto& a = *(next - 1);
	float p = (time - a.time) / (b.time - a.time);
	if (a.interpolation == SegmentInterpolation::Step) {
		return a.value;
	}
	if (a.interpolation == SegmentInterpolation::Bezier) {
		Curve c { { 0, 0 }, { 1, 1 }, a.outControl, a.inControl, {} };
		p = evaluateAtTime(c, p);
	}
	else if (a.interpolation == SegmentInterpolation::Easing) {
		p = getEasingOrLinear(a.easing)(p);
	}
	return a.value + (b.value - a.value) * p;
}

float FUNC::linearInterpolation(float a) {
	return a;
}

float FUNC::quadraticEaseIn(float a) {
	return a * a;
}

float FUNC::quadraticEaseOut(float a) {
	return -(a * (a - static_cast<float>(2)));
}

float FUNC::quadraticEaseInOut(float a) {
	if (a < static_cast<float>(0.5)) {
		return static_cast<float>(2) * a * a;
	}
	else {
		return (-static_cast<float>(2) * a * a) + (4 * a) - 1.0f;
	}
}

float FUNC::cubicEaseIn(float a) {
	return a * a * a;
}

float FUNC::cubicEaseOut(float a) {
	float const f = a - 1.0f;
	return f * f * f + 1.0f;
}

float FUNC::cubicEaseInOut(float a) {
	if (a < static_cast<float>(0.5)) {
		return static_cast<float>(4) * a * a * a;
	}
	else {
		float const f = ((static_cast<float>(2) * a) - static_cast<float>(2));
		return static_cast<float>(0.5) * f * f * f + 1.0f;
	}
}

float FUNC::quarticEaseIn(float a) {
	return a * a * a * a;
}

float FUNC::quarticEaseOut(float a) {
	float const f = (a - 1.0f);
	return f * f * f * (1.0f - a) + 1.0f;
}

float FUNC::quarticEaseInOut(float a) {
	if (a < static_cast<float>(0.5)) {
		return static_cast<float>(8) * a * a * a * a;
	}
	else {
		float const f = (a - 1.0f);
		return -static_cast<float>(8) * f * f * f * f + 1.0f;
	}
}

float FUNC::quinticEaseIn(float a) {
	return a * a * a * a * a;
}

float FUNC::quinticEaseOut(float a) {
	float const f = (a - 1.0f);
	return f * f * f * f * f + 1.0f;
}

float FUNC::quinticEaseInOut(float a) {
	if (a < static_cast<float>(0.5)) {
		return static_cast<float>(16) * a * a * a * a * a;
	}
	else {
		float const f = ((static_cast<float>(2) * a) - static_cast<float>(2));
		return static_cast<float>(0.5) * f * f * f * f * f + 1.0f;
	}
}

float FUNC::sineEaseIn(float a) {
	return sin((a - 1.0f) * (PI / 2.0f)) + 1.0f;
}

float FUNC::sineEaseOut(float a) {
	return sin(a * (PI / 2.0f));
}

float FUNC::sineEaseInOut(float a) {
	return static_cast<float>(0.5) * (1.0f - cos(a * PI));
}

float FUNC::circularEaseIn(float a) {
	return 1.0f - sqrt(1.0f - (a * a));
}

float FUNC::circularEaseOut(float a) {
	return sqrt((static_cast<float>(2) - a) * a);
}

float FUNC::circularEaseInOut(float a) {
	if (a < static_cast<float>(0.5)) {
		return static_cast<float>(0.5) * (1.0f - sqrtf(1.0f - static_cast<float>(4) * (a * a)));
	}
	else {
		return static_cast<float>(0.5)
			* (sqrtf(-((static_cast<float>(2) * a) - static_cast<float>(3)) * ((static_cast<float>(2) * a) - 1.0f)) + 1.0f);
	}
}

float FUNC::exponentialEaseIn(float a) {
	if (a <= 0.0f) {
		return a;
	}
	else {
		float const Complementary = a - 1.0f;
		float const Two = static_cast<float>(2);

		return powf(Two, Complementary * static_cast<float>(10));
	}
}

float FUNC::exponentialEaseOut(float a) {
	if (a >= 1.0f) {
		return a;
	}
	else {
		return 1.0f - powf(static_cast<float>(2), -static_cast<float>(10) * a);
	}
}

float FUNC::exponentialEaseInOut(float a) {
	if (a < static_cast<float>(0.5)) {
		return static_cast<float>(0.5) * powf(static_cast<float>(2), (static_cast<float>(20) * a) - static_cast<float>(10));
	}
	else {
		return -static_cast<float>(0.5) * powf(static_cast<float>(2), (-static_cast<float>(20) * a) + static_cast<float>(10)) + 1.0f;
	}
}

float FUNC::elasticEaseIn(float a) {
	return sinf(static_cast<float>(13) * (PI / 2.0f) * a) * powf(static_cast<float>(2), static_cast<float>(10) * (a - 1.0f));
}

float FUNC::elasticEaseOut(float a) {
	return sinf(-static_cast<float>(13) * (PI / 2.0f) * (a + 1.0f)) * powf(static_cast<float>(2), -static_cast<float>(10) * a) + 1.0f;
}

float FUNC::elasticEaseInOut(float a) {
	if (a < static_cast<float>(0.5)) {
		return static_cast<float>(0.5) * sinf(static_cast<float>(13) * (PI / 2.0f) * (static_cast<float>(2) * a))
			* powf(static_cast<float>(2), static_cast<float>(10) * ((static_cast<float>(2) * a) - 1.0f));
	}
	else {
		return static_cast<float>(0.5)
			* (sinf(-static_cast<float>(13) * (PI / 2.0f) * ((static_cast<float>(2) * a - 1.0f) + 1.0f))
					* powf(static_cast<float>(2), -static_cast<float>(10) * (static_cast<float>(2) * a - 1.0f))
				+ static_cast<float>(2));
	}
}

float FUNC::backEaseInInternal(float a, float o) {
	float z = ((o + 1.0f) * a) - o;
	return (a * a * z);
}

float FUNC::backEaseOutInternal(float a, float o) {
	float n = a - 1.0f;
	float z = ((o + 1.0f) * n) + o;
	return (n * n * z) + 1.0f;
}

float FUNC::backEaseInOutInternal(float a, float o) {
	float s = o * static_cast<float>(1.525);
	float x = static_cast<float>(0.5);
	float n = a / static_cast<float>(0.5);

	if (n < static_cast<float>(1)) {
		float z = ((s + static_cast<float>(1)) * n) - s;
		float m = n * n * z;
		return x * m;
	}
	else {
		n -= static_cast<float>(2);
		float z = ((s + static_cast<float>(1)) * n) + s;
		float m = (n * n * z) + static_cast<float>(2);
		return x * m;
	}
}

float FUNC::backEaseIn(float a) {
	return backEaseInInternal(a, static_cast<float>(1.70158));
}

float FUNC::backEaseOut(float a) {
	return FUNC::backEaseOutInternal(a, static_cast<float>(1.70158));
}

float FUNC::backEaseInOut(float a) {
	return FUNC::backEaseInOutInternal(a, static_cast<float>(1.70158));
}

float FUNC::bounceEaseOut(float a) {
	if (a < static_cast<float>(4.0 / 11.0)) {
		return (static_cast<float>(121) * a * a) / static_cast<float>(16);
	}
	else if (a < static_cast<float>(8.0 / 11.0)) {
		return (static_cast<float>(363.0 / 40.0) * a * a) - (static_cast<float>(99.0 / 10.0) * a) + static_cast<float>(17.0 / 5.0);
	}
	else if (a < static_cast<float>(9.0 / 10.0)) {
		return (static_cast<float>(4356.0 / 361.0) * a * a) - (static_cast<float>(35442.0 / 1805.0) * a) + static_cast<float>(16061.0 / 1805.0);
	}
	else {
		return (static_cast<float>(54.0 / 5.0) * a * a) - (static_cast<float>(513.0 / 25.0) * a) + static_cast<float>(268.0 / 25.0);
	}
}

float FUNC::bounceEaseIn(float a) {
	return 1.0f - bounceEaseOut(1.0f - a);
}

float FUNC::bounceEaseInOut(float a) {
	if (a < static_cast<float>(0.5)) {
		return static_cast<float>(0.5) * (1.0f - bounceEaseOut(a * static_cast<float>(2)));
	}
	else {
		return static_cast<float>(0.5) * bounceEaseOut(a * static_cast<float>(2) - 1.0f) + static_cast<float>(0.5);
	}
}
