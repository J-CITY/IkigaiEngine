#include "math_tests.h"
#include "support/expect.h"

#include "mathModule/math.h"

#include "utilsModule/serialization/jsonAdapters.h"

using IKIGAI::MATH::Vector3f;
using IKIGAI::TEST::expect;
using IKIGAI::TEST::expectNear;
using IKIGAI::TEST::expectThrows;

namespace IKIGAI::TEST::MATH {

void runVector3() {
	const Vector3f a(3.f, 4.f, 0.f);
	const Vector3f b(1.f, 2.f, 3.f);

	const Vector3f sum = a + b;
	expect(sum.x == 4.f && sum.y == 6.f && sum.z == 3.f, "vector3.add");

	const Vector3f diff = a - b;
	expect(diff.x == 2.f && diff.y == 2.f && diff.z == -3.f, "vector3.sub");

	const Vector3f scaled = a * 2.f;
	expect(scaled.x == 6.f && scaled.y == 8.f && scaled.z == 0.f, "vector3.scale");

	expectNear(Vector3f::Dot(a, b), 11.f, "vector3.dot");

	const Vector3f cross = Vector3f::Cross(Vector3f::Right, Vector3f::Up);
	expectNear(cross.x, 0.f, "vector3.cross.x");
	expectNear(cross.y, 0.f, "vector3.cross.y");
	expectNear(cross.z, 1.f, "vector3.cross.z");

	expectNear(Vector3f::Length(a), 5.f, "vector3.length");

	const Vector3f unit = Vector3f::Normalize(Vector3f::Right);
	expectNear(unit.x, 1.f, "vector3.normalize.x");
	expectNear(unit.y, 0.f, "vector3.normalize.y");
	expectNear(unit.z, 0.f, "vector3.normalize.z");

	const Vector3f zeroNorm = Vector3f::Normalize(Vector3f::Zero);
	expect(zeroNorm.x == 0.f && zeroNorm.y == 0.f && zeroNorm.z == 0.f, "vector3.normalize.zero");

	const Vector3f lerp0 = Vector3f::Lerp(a, b, 0.f);
	expect(lerp0 == a, "vector3.lerp.0");
	const Vector3f lerp1 = Vector3f::Lerp(a, b, 1.f);
	expect(lerp1 == b, "vector3.lerp.1");

	expectThrows([] { (void)Vector3f::Divide(Vector3f(1.f, 2.f, 3.f), 0.f); }, "vector3.divide.zero");
	expectThrows(
		[] {
			Vector3f v;
			(void)v[3];
		},
		"vector3.out_of_range");

	Vector3f swizzleSource(7.f, 8.f, 9.f);
	const auto xy = static_cast<IKIGAI::MATH::Vector2f>(swizzleSource.xy());
	expect(xy.x == 7.f && xy.y == 8.f, "vector3.swizzle.xy");

	const Vector3f original(1.5f, -2.f, 3.25f);
	nlohmann::json json = serde::serialize<nlohmann::json>(original);
	const Vector3f roundTrip = serde::deserialize<Vector3f>(json);
	expectNear(roundTrip.x, original.x, "vector3.serde.x");
	expectNear(roundTrip.y, original.y, "vector3.serde.y");
	expectNear(roundTrip.z, original.z, "vector3.serde.z");
}

} // namespace IKIGAI::TEST::MATH
