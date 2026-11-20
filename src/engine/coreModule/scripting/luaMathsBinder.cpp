#if USE_LUA
#include "luaMathsBinder.h"

#include <sol/sol.hpp>

#include "mathModule/math.h"

using namespace IKIGAI::SCRIPTING;

void LuaMathsBinder::BindMaths(sol::state& p_luaState) {
	using namespace IKIGAI::MATH;

	p_luaState.new_usertype<Vector2f>(
		"Vector2", sol::constructors<Vector2f(), Vector2f(float), Vector2f(float, float)>(),

		"x", sol::property(&Vector2f::getX, &Vector2f::setX), "y", sol::property(&Vector2f::getY, &Vector2f::setY),
		//"x", &Vector2f::getX,
		//"y", &Vector2f::getY,

		sol::meta_function::addition, sol::resolve<Vector2f(const Vector2f&) const>(&Vector2f::operator+), sol::meta_function::subtraction,
		sol::resolve<Vector2f(const Vector2f&) const>(&Vector2f::operator-), sol::meta_function::unary_minus,
		sol::resolve<Vector2f() const>(&Vector2f::operator-), sol::meta_function::multiplication,
		static_cast<Vector2f (Vector2f::*)(float) const>(&Vector2f::operator*), sol::meta_function::division, &Vector2f::operator/,
		sol::meta_function::to_string, [](const Vector2f& target) { return "(" + std::to_string(target.x) + "," + std::to_string(target.y) + ")"; },
		"Length", &Vector2f::length, "Dot", &Vector2f::dot, "Normalize", &Vector2f::normalize);

	p_luaState.new_usertype<Vector3f>(
		"Vector3",
		/* Constructors */
		sol::constructors<Vector3f(), Vector3f(const float&), Vector3f(const float&, const float&, const float&)>(),

		/* Operators */
		sol::meta_function::addition, &Vector3f::operator+, sol::meta_function::subtraction,
		sol::resolve<Vector3f(const Vector3f&) const>(&Vector3f::operator-), sol::meta_function::unary_minus,
		sol::resolve<Vector3f() const>(&Vector3f::operator-), sol::meta_function::multiplication,
		sol::resolve<Vector3f(const Vector3f&) const>(&Vector3f::operator*), sol::meta_function::multiplication,
		sol::resolve<Vector3f(float) const>(&Vector3f::operator*), sol::meta_function::division,
		sol::resolve<Vector3f(const Vector3f&) const>(&Vector3f::operator/), sol::meta_function::division,
		sol::resolve<Vector3f(float) const>(&Vector3f::operator/), sol::meta_function::to_string,
		[](const Vector3f& target) { return "(" + std::to_string(target.x) + "," + std::to_string(target.y) + "," + std::to_string(target.z) + ")"; },

		/* Variables */
		"x", sol::property(&Vector3f::getX, &Vector3f::setX), "y", sol::property(&Vector3f::getY, &Vector3f::setY), "z",
		sol::property(&Vector3f::getZ, &Vector3f::setZ),

		/* Data */
		"One", []() { return Vector3f::One; }, "Zero", []() { return Vector3f::Zero; }, "Forward", []() { return Vector3f::Forward; }, "Up",
		[]() { return Vector3f::Up; }, "Right", []() { return Vector3f::Right; }, "Backward", []() { return Vector3f::Forward * -1; }, "Down",
		[]() { return Vector3f::Up * -1; }, "Left", []() { return Vector3f::Right * -1; },

		/* Methods */
		"Length", &Vector3f::Length, "Dot", &Vector3f::Dot, "Cross", &Vector3f::Cross, "Normalize", &Vector3f::Normalize, "Lerp", &Vector3f::Lerp,
		"AngleBetween", &Vector3f::AngleBetween, "Distance", &Vector3f::Distance);

	p_luaState.new_usertype<Vector4f>(
		"Vector4",
		/* Constructors */
		sol::constructors<Vector4f(), Vector4f(float, float, float, float)>(),

		/* Operators */
		// sol::meta_function::addition, &Vector4f::operator+,
		// sol::meta_function::subtraction, sol::resolve<Vector4f(const Vector4f&) const>(&Vector4f::operator-),
		// sol::meta_function::unary_minus, sol::resolve<Vector4f() const>(&Vector4f::operator-),
		// sol::meta_function::multiplication, &Vector4f::operator*,
		// sol::meta_function::division, &Vector4f::operator/,
		sol::meta_function::to_string,
		[](const Vector4f& target) {
			return "(" + std::to_string(target.x) + "," + std::to_string(target.y) + "," + std::to_string(target.z) + "," + std::to_string(target.w)
				+ ")";
		},

		/* Variables */
		"x", sol::property(&Vector4f::getX, &Vector4f::setX), "y", sol::property(&Vector4f::getY, &Vector4f::setY), "z",
		sol::property(&Vector4f::getZ, &Vector4f::setZ), "w", sol::property(&Vector4f::getW, &Vector4f::setW)

		/* Data */
		//"One", []() { return Vector4f::One; },
		//"Zero", []() { return Vector4f::Zero; },

		/* Methods */
		//"Length", &Vector4f::Length,
		//"Dot", &Vector4f::Dot,
		//"Normalize", &Vector4f::Normalize,
		//"Lerp", &Vector4f::Lerp
	);

	p_luaState.new_usertype<Matrix3f>(
		"Matrix3",
		/* Constructors */
		sol::constructors<Matrix3f(), Matrix3f(float), Matrix3f(float, float, float, float, float, float, float, float, float)>(),

		/* Operators */
		sol::meta_function::addition, &Matrix3f::operator+, sol::meta_function::subtraction, &Matrix3f::operator-, sol::meta_function::multiplication,
		sol::overload(sol::resolve<Matrix3f(float) const>(&Matrix3f::operator*), sol::resolve<Vector3f(const Vector3f&) const>(&Matrix3f::operator*),
			sol::resolve<Matrix3f(const Matrix3f&) const>(&Matrix3f::operator*)),
		sol::meta_function::division,
		sol::overload(sol::resolve<Matrix3f(float) const>(&Matrix3f::operator/), sol::resolve<Matrix3f(const Matrix3f&) const>(&Matrix3f::operator/)),
		sol::meta_function::to_string, [](const Matrix3f& target) { return "Can't show matrix as string for now"; },

		/* Data */
		"Identity", []() { return Matrix3f::Identity; },

		/* Methods */
		"IsIdentity", &Matrix3f::IsIdentity, "Determinant", &Matrix3f::Determinant, "Transpose", &Matrix3f::Transpose, "Cofactor",
		&Matrix3f::Cofactor, "Minor", &Matrix3f::Minor, "Adjoint", &Matrix3f::Adjoint, "Inverse", &Matrix3f::Inverse,
		//"Translation", &Matrix3f::Translation,
		//"Translate", &Matrix3f::Translate,
		"Rotation", sol::resolve<Matrix3f(float)>(&Matrix3f::Rotation), "Rotation",
		sol::resolve<Matrix3f(const Matrix3f& matrix, float rotation)>(&Matrix3f::Rotation),
		//"Scaling", &Matrix3f::Scaling,
		//"Scale", &Matrix3f::Scale,
		"GetRow", &Matrix3f::GetRow, "GetColumn", &Matrix3f::GetColumn, "Get", [](Matrix3f& target, int row, int col) { return target(row, col); },
		"Set", [](Matrix3f& target, int row, int col, float value) { target(row, col) = value; });

	p_luaState.new_usertype<Matrix4f>(
		"Matrix4",
		/* Constructors */
		sol::constructors<Matrix4f(),
			Matrix4f(float, float, float, float, float, float, float, float, float, float, float, float, float, float, float, float)>(),

		/* Operators */
		sol::meta_function::addition, &Matrix4f::operator+, sol::meta_function::subtraction,
		sol::overload(sol::resolve<Matrix4f(float) const>(&Matrix4f::operator-), sol::resolve<Matrix4f(const Matrix4f&) const>(&Matrix4f::operator-)),
		sol::meta_function::multiplication,
		sol::overload(sol::resolve<Matrix4f(float) const>(&Matrix4f::operator*), sol::resolve<Vector4f(const Vector4f&) const>(&Matrix4f::operator*),
			sol::resolve<Matrix4f(const Matrix4f&) const>(&Matrix4f::operator*)),
		sol::meta_function::division,
		sol::overload(sol::resolve<Matrix4f(float) const>(&Matrix4f::operator/), sol::resolve<Matrix4f(const Matrix4f&) const>(&Matrix4f::operator/)),
		sol::meta_function::to_string, [](const Matrix4f& target) { return "Can't show matrix as string for now"; },

		/* Data */
		"Identity", []() { return Matrix4f::Identity; },

		/* Methods */
		"IsIdentity", &Matrix4f::IsIdentity, "Determinant", &Matrix4f::Determinant, "Transpose", &Matrix4f::Transpose, "Minor", &Matrix4f::GetMinor,
		"Inverse", &Matrix4f::Inverse, "Translation", &Matrix4f::Translation,
		//"Translate", &Matrix4f::Translate,
		//"RotationOnAxisX", &Matrix4f::RotationOnAxisX,
		//"RotateOnAxisX", &Matrix4f::RotateOnAxisX,
		//"RotationOnAxisY", &Matrix4f::RotationOnAxisY,
		//"RotateOnAxisY", &Matrix4f::RotateOnAxisY,
		//"RotationOnAxisZ", &Matrix4f::RotationOnAxisZ,
		//"RotateOnAxisZ", &Matrix4f::RotateOnAxisZ,
		//"RotationYXZ", &Matrix4f::RotationYXZ,
		//"RotateYXZ", &Matrix4f::RotateYXZ,
		//"Scaling", &Matrix4f::Scaling,
		//"Scale", &Matrix4f::Scale,
		//"GetRow", &Matrix4f::GetRow,
		//"GetColumn", &Matrix4f::GetColumn,
		"CreatePerspective", &Matrix4f::CreatePerspective,
		//"CreateView", &Matrix4f::CreateView,
		"CreateFrustum", &Matrix4f::CreateFrustum, "Get", [](Matrix4f& target, int row, int col) { return target(row, col); }, "Set",
		[](Matrix4f& target, int row, int col, float value) { target(row, col) = value; });

	auto RotatePointOverload
		= sol::overload(sol::resolve<Vector3f(const Vector3f&, const QuaternionF&)>(&QuaternionF::RotatePoint), // Rotate without pivot
			sol::resolve<Vector3f(const Vector3f&, const QuaternionF&, const Vector3f&)>(&QuaternionF::RotatePoint) // Rotate with pivot
		);

	p_luaState.new_usertype<QuaternionF>(
		"Quaternion",
		/* Constructors */
		sol::constructors<QuaternionF(), QuaternionF(float), QuaternionF(float, float, float, float), QuaternionF(const Vector3f&)>(),

		/* Operators */
		sol::meta_function::addition, &QuaternionF::operator+, sol::meta_function::subtraction, &QuaternionF::operator-, sol::meta_function::division,
		&QuaternionF::operator/, sol::meta_function::multiplication,
		sol::overload(static_cast<QuaternionF (QuaternionF::*)(float) const>(
						  &QuaternionF::operator*), // sol::resolve<QuaternionF(float) const>(&QuaternionF::operator*),
			sol::resolve<QuaternionF(const QuaternionF&) const>(&QuaternionF::operator*),
			sol::resolve<Matrix3f(const Matrix3f&) const>(&QuaternionF::operator*),
			sol::resolve<Vector3f(const Vector3f&) const>(&QuaternionF::operator*)),
		sol::meta_function::to_string,
		[](const QuaternionF& target) {
			return "(" + std::to_string(target.x) + "," + std::to_string(target.y) + "," + std::to_string(target.z) + "," + std::to_string(target.w)
				+ ")";
		},

		/* Methods */
		//"IsIdentity", &QuaternionF::IsIdentity,
		//"IsPure", &QuaternionF::IsPure,
		"IsNormalized", &QuaternionF::IsNormalized,
		//"Dot", &QuaternionF::DotProduct,
		"Normalize", &QuaternionF::Normalize, "Length", &QuaternionF::Length, "LengthSquare", &QuaternionF::LengthSquare, "ToQuaternion",
		&QuaternionF::ToQuaternion,
		//"GetAngle", &QuaternionF::GetAngle,
		//"GetRotationAxis", &QuaternionF::GetRotationAxis,
		//"Inverse", &QuaternionF::Inverse,
		//"Conjugate", &QuaternionF::Conjugate,
		//"Square", &QuaternionF::Square,
		//"GetAxisAndAngle", &QuaternionF::GetAxisAndAngle,
		//"AngularDistance", &QuaternionF::AngularDistance,
		//"Lerp", &QuaternionF::Lerp,
		//"Slerp", &QuaternionF::Slerp,
		//"Nlerp", &QuaternionF::Nlerp,
		"RotatePoint", RotatePointOverload,
		//"EulerAngles", &QuaternionF::EulerAngles,
		"ToMatrix3f", &QuaternionF::ToMatrix3, "ToMatrix4f", &QuaternionF::ToMatrix4,

		/* Variables */
		"x", sol::property(&QuaternionF::getX, &QuaternionF::setX), "y", sol::property(&QuaternionF::getY, &QuaternionF::setY), "z",
		sol::property(&QuaternionF::getZ, &QuaternionF::setZ), "w", sol::property(&QuaternionF::getW, &QuaternionF::setW));
}

#endif
