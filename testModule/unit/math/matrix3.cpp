#include "math_tests.h"
#include "support/expect.h"

#include "mathModule/math.h"

#include <limits>
#include <stdexcept>

using IKIGAI::MATH::Matrix3f;
using IKIGAI::MATH::Vector3f;
using IKIGAI::TEST::expect;
using IKIGAI::TEST::expectNear;
using IKIGAI::TEST::expectThrows;

namespace IKIGAI::TEST::MATH {
namespace {

	float at(const Matrix3f& m, unsigned row, unsigned col) {
		return m.getData()[3 * row + col];
	}

	bool matrixNearIdentity(const Matrix3f& m, float eps = 1e-4f) {
		for (unsigned row = 0; row < 3; ++row) {
			for (unsigned col = 0; col < 3; ++col) {
				const float expected = row == col ? 1.f : 0.f;
				if (!near(at(m, row, col), expected, eps)) {
					return false;
				}
			}
		}
		return true;
	}

	bool matrixNear(const Matrix3f& a, const Matrix3f& b, float eps = 1e-4f) {
		for (unsigned row = 0; row < 3; ++row) {
			for (unsigned col = 0; col < 3; ++col) {
				if (!near(at(a, row, col), at(b, row, col), eps)) {
					return false;
				}
			}
		}
		return true;
	}

} // namespace

void runMatrix3() {
	const Matrix3f identity = Matrix3f::Identity;
	expectNear(Matrix3f::Determinant(identity), 1.f, "matrix3.det.identity");
	const Matrix3f nonFinite(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, std::numeric_limits<float>::quiet_NaN());
	expect(!matrixNearIdentity(nonFinite) && !matrixNear(nonFinite, identity), "matrix3.comparisons.rejectNaN");

	const Matrix3f m(2.f, 0.f, 0.f, 0.f, 3.f, 0.f, 0.f, 0.f, 4.f);
	expect(matrixNear(identity * m, m), "matrix3.identity.multiply");

	const Matrix3f twice = Matrix3f::Transpose(Matrix3f::Transpose(m));
	expect(matrixNear(twice, m), "matrix3.transpose.roundtrip");

	const Matrix3f inv = Matrix3f::Inverse(m);
	expect(matrixNearIdentity(inv * m), "matrix3.inverse.roundtrip");

	// data[3 * row + col]: column 0 of [[1,2,3],[4,5,6],[7,8,9]] is (1,4,7).
	const Matrix3f numbered(1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f);
	const Vector3f col0 = Matrix3f::GetColumn(numbered, 0);
	expectNear(col0.x, 1.f, "matrix3.column.x");
	expectNear(col0.y, 4.f, "matrix3.column.y");
	expectNear(col0.z, 7.f, "matrix3.column.z");

	expectThrows<std::logic_error>([] { (void)Matrix3f::Inverse(Matrix3f::Zero); }, "matrix3.inverse.singular");
}

} // namespace IKIGAI::TEST::MATH
