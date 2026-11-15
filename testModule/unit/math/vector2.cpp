#include "math_tests.h"
#include "support/expect.h"

#include "mathModule/vector2.h"

using IKIGAI::MATH::Vector2f;
using IKIGAI::TEST::expect;
using IKIGAI::TEST::expectNear;
using IKIGAI::TEST::expectThrows;

namespace IKIGAI::TEST::MATH {

void runVector2() {
	const Vector2f a(3.f, 4.f);
	const Vector2f b(1.f, 2.f);

	const Vector2f sum = a + b;
	expect(sum.x == 4.f && sum.y == 6.f, "vector2.add");

	const Vector2f diff = a - b;
	expect(diff.x == 2.f && diff.y == 2.f, "vector2.sub");

	const Vector2f scaled = a * 2.f;
	expect(scaled.x == 6.f && scaled.y == 8.f, "vector2.scale");

	expectNear(a.dot(b), 11.f, "vector2.dot");
	expectNear(a.length(), 5.f, "vector2.length");

	const Vector2f unit = Vector2f(1.f, 0.f).normalize();
	expectNear(unit.x, 1.f, "vector2.normalize.x");
	expectNear(unit.y, 0.f, "vector2.normalize.y");

	expectThrows(
		[] {
			Vector2f v;
			(void)v[2];
		},
		"vector2.out_of_range");
}

} // namespace IKIGAI::TEST::MATH
