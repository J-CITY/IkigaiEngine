#include "math_tests.h"
#include "support/expect.h"

#include "mathModule/math.h"

#include <cmath>

using IKIGAI::MATH::Vector4f;
using IKIGAI::TEST::expect;
using IKIGAI::TEST::expectNear;
using IKIGAI::TEST::expectThrows;

namespace IKIGAI::TEST::MATH {

void runVector4() {
	const Vector4f a(1.f, 2.f, 3.f, 4.f);
	const Vector4f b(5.f, 6.f, 7.f, 8.f);

	const Vector4f sum = a + b;
	expectNear(sum.x, 6.f, "vector4.add.x");
	expectNear(sum.y, 8.f, "vector4.add.y");
	expectNear(sum.z, 10.f, "vector4.add.z");
	expectNear(sum.w, 12.f, "vector4.add.w");

	const Vector4f diff = a - b;
	expectNear(diff.x, -4.f, "vector4.sub.x");
	expectNear(diff.y, -4.f, "vector4.sub.y");
	expectNear(diff.z, -4.f, "vector4.sub.z");
	expectNear(diff.w, -4.f, "vector4.sub.w");

	const Vector4f scaled = a * 2.f;
	expectNear(scaled.x, 2.f, "vector4.scale.x");
	expectNear(scaled.y, 4.f, "vector4.scale.y");
	expectNear(scaled.z, 6.f, "vector4.scale.z");
	expectNear(scaled.w, 8.f, "vector4.scale.w");

	expectNear(Vector4f::Dot(a, b), 70.f, "vector4.dot");
	expectNear(Vector4f::Length(Vector4f(1.f, 2.f, 2.f, 4.f)), 5.f, "vector4.length");

	const Vector4f unit = Vector4f::Normalize(Vector4f(0.f, 0.f, 0.f, 3.f));
	expectNear(unit.x, 0.f, "vector4.normalize.x");
	expectNear(unit.y, 0.f, "vector4.normalize.y");
	expectNear(unit.z, 0.f, "vector4.normalize.z");
	expectNear(unit.w, 1.f, "vector4.normalize.w");

	// Same contract as Vector3::Normalize: zero input yields zero, no divide-by-zero.
	bool zeroNormalizeSafe = false;
	try {
		const Vector4f zeroNorm = Vector4f::Normalize(Vector4f::Zero);
		zeroNormalizeSafe = zeroNorm.x == 0.f && zeroNorm.y == 0.f && zeroNorm.z == 0.f && zeroNorm.w == 0.f && std::isfinite(zeroNorm.x)
			&& std::isfinite(zeroNorm.y) && std::isfinite(zeroNorm.z) && std::isfinite(zeroNorm.w);
	} catch (...) {
		zeroNormalizeSafe = false;
	}
	expect(zeroNormalizeSafe, "vector4.normalize.zero");

	expectThrows(
		[] {
			Vector4f v;
			(void)v[4];
		},
		"vector4.out_of_range");
}

} // namespace IKIGAI::TEST::MATH
