#include "math_tests.h"
#include "support/expect.h"

#include "mathModule/math.h"

#include <cmath>
#include <cstdint>

using IKIGAI::MATH::Matrix4f;
using IKIGAI::MATH::TO_RADIANS;
using IKIGAI::MATH::Vector3f;
using IKIGAI::MATH::Vector4f;
using IKIGAI::TEST::expect;
using IKIGAI::TEST::expectNear;
using IKIGAI::TEST::expectThrows;

namespace IKIGAI::TEST::MATH {
namespace {

	float at(const Matrix4f& m, unsigned row, unsigned col) {
		return m.getData()[4 * row + col];
	}

	bool matrixNearIdentity(const Matrix4f& m, float eps = 1e-4f) {
		for (unsigned row = 0; row < 4; ++row) {
			for (unsigned col = 0; col < 4; ++col) {
				const float expected = row == col ? 1.f : 0.f;
				if (std::fabs(at(m, row, col) - expected) > eps) {
					return false;
				}
			}
		}
		return true;
	}

} // namespace

void runMatrix4() {
	const Matrix4f translation = Matrix4f::Translation(Vector3f(2.f, 3.f, 4.f));
	const Vector3f moved = translation * Vector3f(1.f, 1.f, 1.f);
	expectNear(moved.x, 3.f, "matrix4.translation.x");
	expectNear(moved.y, 4.f, "matrix4.translation.y");
	expectNear(moved.z, 5.f, "matrix4.translation.z");

	const Matrix4f rotY = Matrix4f::Rotation(TO_RADIANS(90.f), Vector3f::Up);
	const Vector4f rotated = rotY * Vector4f(1.f, 0.f, 0.f, 1.f);
	expectNear(rotated.x, 0.f, "matrix4.rotateY.x");
	expectNear(rotated.y, 0.f, "matrix4.rotateY.y");
	expectNear(rotated.z, -1.f, "matrix4.rotateY.z");

	const Matrix4f m = Matrix4f::Scaling(Vector3f(2.f, 3.f, 4.f)) * Matrix4f::Translation(Vector3f(1.f, 2.f, 3.f));
	const Matrix4f inv = Matrix4f::Inverse(m);
	expect(matrixNearIdentity(inv * m), "matrix4.inverse.roundtrip");

	const Matrix4f ortho = Matrix4f::CreateOrthographic(-1.f, 1.f, -1.f, 1.f, 1.f, 3.f);
	const Vector4f nearPoint = ortho * Vector4f(0.f, 0.f, -1.f, 1.f);
	const Vector4f farPoint = ortho * Vector4f(0.f, 0.f, -3.f, 1.f);
	expectNear(nearPoint.z / nearPoint.w, -1.f, "matrix4.ortho.near");
	expectNear(farPoint.z / farPoint.w, 1.f, "matrix4.ortho.far");

	const Vector3f eye(0.f, 0.f, 5.f);
	const Vector3f look(0.f, 0.f, 0.f);
	const Vector3f up(0.f, 1.f, 0.f);
	const Matrix4f view = Matrix4f::CreateView(eye, look, up);
	const Vector3f eyeInView = view * eye;
	expectNear(eyeInView.x, 0.f, "matrix4.view.eye.x");
	expectNear(eyeInView.y, 0.f, "matrix4.view.eye.y");
	expectNear(eyeInView.z, 0.f, "matrix4.view.eye.z");

	// CreateView stores basis vectors as rows 0..2.
	const Vector3f rightAxis(view.getData()[0], view.getData()[1], view.getData()[2]);
	const Vector3f upAxis(view.getData()[4], view.getData()[5], view.getData()[6]);
	const Vector3f forwardAxis(view.getData()[8], view.getData()[9], view.getData()[10]);
	expectNear(Vector3f::Length(rightAxis), 1.f, "matrix4.view.right.len");
	expectNear(Vector3f::Length(upAxis), 1.f, "matrix4.view.up.len");
	expectNear(Vector3f::Length(forwardAxis), 1.f, "matrix4.view.forward.len");
	expectNear(Vector3f::Dot(rightAxis, upAxis), 0.f, "matrix4.view.right.up");
	expectNear(Vector3f::Dot(rightAxis, forwardAxis), 0.f, "matrix4.view.right.forward");
	expectNear(Vector3f::Dot(upAxis, forwardAxis), 0.f, "matrix4.view.up.forward");

	expectThrows([] { (void)Matrix4f::Inverse(Matrix4f::Zero); }, "matrix4.inverse.singular");
}

} // namespace IKIGAI::TEST::MATH
