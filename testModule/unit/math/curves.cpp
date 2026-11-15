#include "math_tests.h"
#include "support/expect.h"

#include "mathModule/curves.h"

using IKIGAI::MATH::CURVES::Curve;
using IKIGAI::MATH::CURVES::evaluateAtTime;
using IKIGAI::MATH::CURVES::FloatCurve;
using IKIGAI::MATH::CURVES::FloatKey;
using IKIGAI::MATH::CURVES::getEasing;
using IKIGAI::MATH::CURVES::getEasingOrLinear;
using IKIGAI::MATH::CURVES::InterpolationType;
using IKIGAI::MATH::CURVES::makeUnitBezierCurve;
using IKIGAI::MATH::CURVES::SegmentInterpolation;
using IKIGAI::TEST::expect;
using IKIGAI::TEST::expectNear;
using IKIGAI::TEST::expectThrows;

namespace IKIGAI::TEST::MATH {
namespace {

	void expectEasingEnds(InterpolationType type, const char* zeroName, const char* oneName) {
		const auto fn = getEasing(type);
		expect(fn != nullptr, zeroName);
		expectNear(fn(0.f), 0.f, zeroName);
		expectNear(fn(1.f), 1.f, oneName);
	}

} // namespace

void runCurves() {
	expectEasingEnds(InterpolationType::LINEAR, "curves.linear.0", "curves.linear.1");
	expectEasingEnds(InterpolationType::QUADRATIC_IN, "curves.quadratic_in.0", "curves.quadratic_in.1");
	expectEasingEnds(InterpolationType::CUBIC_IN, "curves.cubic_in.0", "curves.cubic_in.1");
	expectEasingEnds(InterpolationType::SINE_IN, "curves.sine_in.0", "curves.sine_in.1");
	expectEasingEnds(InterpolationType::BOUNCE_OUT, "curves.bounce_out.0", "curves.bounce_out.1");

	expect(getEasing(InterpolationType::QUADRIC_IN) == getEasing(InterpolationType::QUARTIC_IN), "curves.quadric.alias.in");
	expect(getEasing(InterpolationType::QUADRIC_OUT) == getEasing(InterpolationType::QUARTIC_OUT), "curves.quadric.alias.out");
	expect(getEasing(InterpolationType::QUADRIC_IN_OUT) == getEasing(InterpolationType::QUARTIC_IN_OUT), "curves.quadric.alias.in_out");

	expect(getEasing(InterpolationType::CUSTOM) == nullptr, "curves.custom.null");
	expect(getEasingOrLinear(InterpolationType::CUSTOM) == &IKIGAI::MATH::CURVES::FUNC::linearInterpolation, "curves.custom.fallback");

	const Curve unit = makeUnitBezierCurve();
	expectNear(evaluateAtTime(unit, 0.5f), 0.5f, "curves.bezier.mid");

	FloatCurve linear;
	linear.keys = {
		FloatKey { 0.f, 0.f, SegmentInterpolation::Linear },
		FloatKey { 2.f, 10.f, SegmentInterpolation::Linear },
	};
	linear.validate();
	expectNear(linear.evaluate(1.f), 5.f, "curves.float.linear.mid");

	FloatCurve step;
	step.keys = {
		FloatKey { 0.f, 3.f, SegmentInterpolation::Step },
		FloatKey { 1.f, 9.f, SegmentInterpolation::Linear },
	};
	step.validate();
	expectNear(step.evaluate(0.5f), 3.f, "curves.float.step");

	expectThrows(
		[] {
			FloatCurve empty;
			empty.validate();
		},
		"curves.float.empty");

	expectThrows(
		[] {
			FloatCurve unordered;
			unordered.keys = {
				FloatKey { 1.f, 0.f, SegmentInterpolation::Linear },
				FloatKey { 0.f, 1.f, SegmentInterpolation::Linear },
			};
			unordered.validate();
		},
		"curves.float.unordered");
}

} // namespace IKIGAI::TEST::MATH
