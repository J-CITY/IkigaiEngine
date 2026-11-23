#include "math_tests.h"
#include "support/expect.h"

#include "mathModule/math.h"

#include <cmath>
#include <stdexcept>

using IKIGAI::MATH::Matrix4f;
using IKIGAI::MATH::QuaternionF;
using IKIGAI::MATH::TO_RADIANS;
using IKIGAI::MATH::Vector3f;
using IKIGAI::MATH::Vector4f;
using IKIGAI::TEST::expect;
using IKIGAI::TEST::expectNear;
using IKIGAI::TEST::expectThrows;

namespace IKIGAI::TEST::MATH {
namespace {

	bool quatNear(const QuaternionF& a, const QuaternionF& b, float eps = 1e-4f) {
		return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps && std::fabs(a.w - b.w) <= eps;
	}

} // namespace

void runQuaternion() {
	expectNear(QuaternionF::Length(QuaternionF::Identity), 1.f, "quaternion.identity.length");

	const QuaternionF q = QuaternionF::Normalize(QuaternionF(0.f, 0.70710678f, 0.f, 0.70710678f));
	const QuaternionF product = q * QuaternionF::Conjugate(q);
	expectNear(product.x, 0.f, "quaternion.conjugate.x");
	expectNear(product.y, 0.f, "quaternion.conjugate.y");
	expectNear(product.z, 0.f, "quaternion.conjugate.z");
	expectNear(product.w, 1.f, "quaternion.conjugate.w");

	const QuaternionF rotY = QuaternionF::Normalize(QuaternionF(0.f, std::sin(TO_RADIANS(90.f) * 0.5f), 0.f, std::cos(TO_RADIANS(90.f) * 0.5f)));
	const Vector3f rotatedByQuat = QuaternionF::RotatePoint(Vector3f(1.f, 0.f, 0.f), rotY);
	const Matrix4f rotMatrix = Matrix4f::Rotation(TO_RADIANS(90.f), Vector3f::Up);
	const Vector4f rotatedByMatrix = rotMatrix * Vector4f(1.f, 0.f, 0.f, 1.f);
	expectNear(rotatedByQuat.x, rotatedByMatrix.x, "quaternion.rotate.matches.matrix.x");
	expectNear(rotatedByQuat.y, rotatedByMatrix.y, "quaternion.rotate.matches.matrix.y");
	expectNear(rotatedByQuat.z, rotatedByMatrix.z, "quaternion.rotate.matches.matrix.z");
	expectNear(rotatedByQuat.x, 0.f, "quaternion.rotateY.x");
	expectNear(rotatedByQuat.y, 0.f, "quaternion.rotateY.y");
	expectNear(rotatedByQuat.z, -1.f, "quaternion.rotateY.z");

	const QuaternionF left(0.1f, 0.2f, 0.3f, 0.4f);
	const QuaternionF right(0.5f, 0.6f, 0.7f, 0.8f);
	QuaternionF assigned = left;
	assigned *= right;
	expect(quatNear(assigned, left * right), "quaternion.mul.assign");

	const QuaternionF a = QuaternionF::Normalize(QuaternionF(0.f, 0.f, 0.f, 1.f));
	const QuaternionF b = rotY;
	expect(quatNear(QuaternionF::Slerp(a, b, 0.f), a), "quaternion.slerp.0");
	expect(quatNear(QuaternionF::Slerp(a, b, 1.f), b), "quaternion.slerp.1");
	// At one third of a quarter turn the rotation is 30 degrees.
	const auto third = QuaternionF::Slerp(a, b, 1.f / 3.f);
	const auto point = QuaternionF::RotatePoint(Vector3f { 1.f, 0.f, 0.f }, third);
	expectNear(point.x, std::sqrt(3.f) / 2.f, "quaternion.slerp.third.x");
	expectNear(point.y, 0.f, "quaternion.slerp.third.y");
	expectNear(point.z, -0.5f, "quaternion.slerp.third.z");

	expectThrows<std::logic_error>(
		[] {
			const QuaternionF denorm(2.f, 0.f, 0.f, 0.f);
			(void)QuaternionF::ToMatrix4(denorm);
		},
		"quaternion.toMatrix4.denorm");
}

} // namespace IKIGAI::TEST::MATH
