#pragma once
#include "vector2.h"
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#if defined(__SSE__) || defined(_M_IX86) || defined(_M_X64)
#include <xmmintrin.h>
#define IKIGAI_USE_SSE
#elif defined(__ARM_NEON) || defined(_M_ARM64)
#include <arm_neon.h>
#define IKIGAI_USE_NEON
#endif

namespace IKIGAI::MATH {
template <class T> struct Quaternion;

constexpr float PI = 3.14159265359f;
constexpr float EPSILON = 10e-7;
constexpr float SLEEP_EPS = 0.3f;

template <typename T> constexpr auto TO_RADIANS(const T& value) {
	return value * PI / 180.f;
}

template <typename T> constexpr auto TO_DEGREES(const T& value) {
	return value * 180.f / PI;
}

inline bool CMP(float a, float b, float eps = 0.00001f) {
	return fabs(a - b) < eps;
}

// Vector3d
template <typename T> struct Vector3 {
	T x {};
	T y {};
	T z {};
	T& r() {
		return x;
	}
	const T& r() const {
		return x;
	}
	T& g() {
		return y;
	}
	const T& g() const {
		return y;
	}
	T& b() {
		return z;
	}
	const T& b() const {
		return z;
	}
	template <template <typename> class Result, int... Indices> auto swizzle() & {
		return Swizzle<Result, T, Indices...>({ &(*this)[Indices]... });
	}
	template <template <typename> class Result, int... Indices> Result<T> swizzle() const& {
		return Result<T>((*this)[Indices]...);
	}
#define IKIGAI_VECTOR_SWIZZLE(Result, Name, ...)                                                                                                     \
	auto Name() & {                                                                                                                                  \
		return swizzle<Result, __VA_ARGS__>();                                                                                                       \
	}                                                                                                                                                \
	Result<T> Name() const& {                                                                                                                        \
		return swizzle<Result, __VA_ARGS__>();                                                                                                       \
	}
	IKIGAI_VECTOR_SWIZZLE(Vector2, xx, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector2, rr, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector2, xy, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector2, rg, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector2, yx, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector2, gr, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector2, yy, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector2, gg, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, xxx, 0, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, rrr, 0, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, xxy, 0, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, rrg, 0, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, xxz, 0, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, rrb, 0, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, xyx, 0, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, rgr, 0, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, xyy, 0, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, rgg, 0, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, xyz, 0, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, rgb, 0, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, xzx, 0, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, rbr, 0, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, xzy, 0, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, rbg, 0, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, xzz, 0, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, rbb, 0, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, yxx, 1, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, grr, 1, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, yxy, 1, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, grg, 1, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, yxz, 1, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, grb, 1, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, yyx, 1, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, ggr, 1, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, yyy, 1, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, ggg, 1, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, yyz, 1, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, ggb, 1, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, yzx, 1, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, gbr, 1, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, yzy, 1, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, gbg, 1, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, yzz, 1, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, gbb, 1, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, zxx, 2, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, brr, 2, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, zxy, 2, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, brg, 2, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, zxz, 2, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, brb, 2, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, zyx, 2, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, bgr, 2, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, zyy, 2, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, bgg, 2, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, zyz, 2, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, bgb, 2, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, zzx, 2, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, bbr, 2, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, zzy, 2, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, bbg, 2, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, zzz, 2, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, bbb, 2, 2, 2)
#undef IKIGAI_VECTOR_SWIZZLE
	std::array<T, 3> toArray() const {
		return { x, y, z };
	}
	void fromArray(const std::array<T, 3>& values) {
		x = values[0];
		y = values[1];
		z = values[2];
	}

	Vector3()
		: x(0)
		, y(0)
		, z(0) { };
	Vector3(const T& _v)
		: x(_v)
		, y(_v)
		, z(_v) { };
	Vector3(const T& _x, const T& _y, const T& _z)
		: x(_x)
		, y(_y)
		, z(_z) { };

	static const Vector3 One;
	static const Vector3 Zero;
	static const Vector3 Forward;
	static const Vector3 Right;
	static const Vector3 Up;

	T getX() {
		return x;
	}
	T getY() {
		return y;
	}
	T getZ() {
		return z;
	}
	void setX(T v) {
		x = v;
	}
	void setY(T v) {
		y = v;
	}
	void setZ(T v) {
		z = v;
	}

	Vector3 operator-() const {
		return operator*(-1);
	}

	Vector3& operator=(const Vector3& other) {
		x = other.x;
		y = other.y;
		z = other.z;
		return *this;
	}

	Vector3 operator+(const Vector3& other) const {
		return Add(*this, other);
	}

	Vector3& operator+=(const Vector3& other) {
		*this = Add(*this, other);
		return *this;
	}

	Vector3 operator-(const Vector3& other) const {
		return Substract(*this, other);
	}

	Vector3& operator-=(const Vector3& other) {
		*this = Substract(*this, other);
		return *this;
	}

	Vector3 operator*(float scalar) const {
		return Multiply(*this, scalar);
	}

	Vector3 operator*(const Vector3& vec) const {
		return Vector3(x * vec.x, y * vec.y, z * vec.z);
	}

	Vector3& operator*=(float scalar) {
		*this = Multiply(*this, scalar);
		return *this;
	}

	Vector3 operator/(float scalar) const {
		return Divide(*this, scalar);
	}

	Vector3 operator/(const Vector3& right) const {
		return Divide(*this, right);
	}

	Vector3& operator/=(float scalar) {
		*this = Divide(*this, scalar);
		return *this;
	}

	static Vector3 Add(const Vector3& left, const Vector3& right) {
		return Vector3(left.x + right.x, left.y + right.y, left.z + right.z);
	}

	static Vector3 Substract(const Vector3& left, const Vector3& right) {
		return Vector3(left.x - right.x, left.y - right.y, left.z - right.z);
	}

	static Vector3 Multiply(const Vector3& target, float scalar) {
		return Vector3(target.x * scalar, target.y * scalar, target.z * scalar);
	}

	static Vector3 Divide(const Vector3& left, float scalar) {
		Vector3 result(left);

		if (CMP(scalar, 0.0f)) {
			throw std::logic_error("Division by 0");
		}

		result.x /= scalar;
		result.y /= scalar;
		result.z /= scalar;

		return result;
	}

	static Vector3 Divide(const Vector3& left, const Vector3& right) {
		Vector3 result(left);

		if (CMP(right.x, 0.0f) || CMP(right.y, 0.0f) || CMP(right.z, 0.0f)) {
			throw std::logic_error("Division by 0");
		}
		result.x /= right.x;
		result.y /= right.y;
		result.z /= right.z;

		return result;
	}

	static float Length(const Vector3& target) {
		return std::sqrt(target.x * target.x + target.y * target.y + target.z * target.z);
	}

	static float LengthSquare(const Vector3& target) {
		return (target.x * target.x + target.y * target.y + target.z * target.z);
	}

	static float Dot(const Vector3& left, const Vector3& right) {
		return left.x * right.x + left.y * right.y + left.z * right.z;
	}

	static float Distance(const Vector3& left, const Vector3& right) {
		return std::sqrt((left.x - right.x) * (left.x - right.x) + (left.y - right.y) * (left.y - right.y) + (left.z - right.z) * (left.z - right.z));
	}

	static Vector3 Cross(const Vector3& left, const Vector3& right) {
		return Vector3(left.y * right.z - left.z * right.y, left.z * right.x - left.x * right.z, left.x * right.y - left.y * right.x);
	}

	static Vector3 Normalize(const Vector3& target) {
		const float length = Length(target);
		if (length > 0.0f) {
			const float targetLength = 1.0f / length;
			return Vector3(target.x * targetLength, target.y * targetLength, target.z * targetLength);
		}
		else {
			return Vector3();
		}
	}

	static Vector3 Lerp(const Vector3& start, const Vector3& end, float alpha) {
		return (start + (end - start) * alpha);
	}

	static Vector3 Mix(const Vector3& x, const Vector3& y, float alpha) {
		return x * (1.0f - alpha) + y * alpha;
	}

	static float AngleBetween(const Vector3& from, const Vector3& to) {
		float lengthProduct = Length(from) * Length(to);

		if (lengthProduct > 0.0f) {
			float fractionResult = Dot(from, to) / lengthProduct;

			if (fractionResult >= -1.0f && fractionResult <= 1.0f) {
				return acosf(fractionResult);
			}
		}

		return 0.0f;
	}

	void clear() {
		x = y = z = 0.0f;
	}

	void addScaledVector(const Vector3& vector, float scale) {
		x += vector.x * scale;
		y += vector.y * scale;
		z += vector.z * scale;
	}
	float dot(const Vector3& vector) const {
		return Dot(*this, vector);
	}
	Vector3 componentProduct(const Vector3& vector) const {
		return Vector3(x * vector.x, y * vector.y, z * vector.z);
	}
	Vector3 cross(const Vector3& vector) const {
		return Cross(*this, vector);
	}
	float magnitude() const {
		return sqrt(x * x + y * y + z * z);
	}
	float squareMagnitude() const {
		return x * x + y * y + z * z;
	}
	void normalise() {
		float l = magnitude();
		if (l > 0) {
			(*this) *= 1.f / l;
		}
	}
	void componentProductUpdate(const Vector3& vector) {
		x *= vector.x;
		y *= vector.y;
		z *= vector.z;
	}

	float& operator[](unsigned i) {
		switch (i) {
		case 0:
			return x;
		case 1:
			return y;
		case 2:
			return z;
		default:
			break;
		}
		throw std::out_of_range("Vector2: out of range");
	};
	float operator[](unsigned i) const {
		switch (i) {
		case 0:
			return x;
		case 1:
			return y;
		case 2:
			return z;
		default:
			break;
		}
		throw std::out_of_range("Vector2: out of range");
	}

	template <class Context> constexpr static void serde(Context& context, Vector3& value);
};

template <typename T> Vector3<T> operator*(const float f, const Vector3<T>& V) {
	return V * f;
}
template <typename T> Vector3<T> operator/(const float f, const Vector3<T>& V) {
	return V / f;
}

template <typename T> bool operator==(const Vector3<T>& left, const Vector3<T>& right) {
	return left.x == right.x && left.y == right.y && left.z == right.z;
}
template <typename T> bool operator!=(const Vector3<T>& left, const Vector3<T>& right) {
	return !operator==(left, right);
}

// Vector4d
template <typename T> struct Vector4 {
	T x {};
	T y {};
	T z {};
	T w {};
	T& r() {
		return x;
	}
	const T& r() const {
		return x;
	}
	T& g() {
		return y;
	}
	const T& g() const {
		return y;
	}
	T& b() {
		return z;
	}
	const T& b() const {
		return z;
	}
	T& a() {
		return w;
	}
	const T& a() const {
		return w;
	}
	template <template <typename> class Result, int... Indices> auto swizzle() & {
		return Swizzle<Result, T, Indices...>({ &(*this)[Indices]... });
	}
	template <template <typename> class Result, int... Indices> Result<T> swizzle() const& {
		return Result<T>((*this)[Indices]...);
	}
#define IKIGAI_VECTOR_SWIZZLE(Result, Name, ...)                                                                                                     \
	auto Name() & {                                                                                                                                  \
		return swizzle<Result, __VA_ARGS__>();                                                                                                       \
	}                                                                                                                                                \
	Result<T> Name() const& {                                                                                                                        \
		return swizzle<Result, __VA_ARGS__>();                                                                                                       \
	}
	IKIGAI_VECTOR_SWIZZLE(Vector2, xx, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector2, rr, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector2, xy, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector2, rg, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector2, yx, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector2, gr, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector2, yy, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector2, gg, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, xxx, 0, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, rrr, 0, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, xxy, 0, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, rrg, 0, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, xxz, 0, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, rrb, 0, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, xyx, 0, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, rgr, 0, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, xyy, 0, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, rgg, 0, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, xyz, 0, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, rgb, 0, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, xzx, 0, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, rbr, 0, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, xzy, 0, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, rbg, 0, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, xzz, 0, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, rbb, 0, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, yxx, 1, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, grr, 1, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, yxy, 1, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, grg, 1, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, yxz, 1, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, grb, 1, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, yyx, 1, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, ggr, 1, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, yyy, 1, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, ggg, 1, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, yyz, 1, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, ggb, 1, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, yzx, 1, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, gbr, 1, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, yzy, 1, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, gbg, 1, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, yzz, 1, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, gbb, 1, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, zxx, 2, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, brr, 2, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, zxy, 2, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, brg, 2, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, zxz, 2, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, brb, 2, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, zyx, 2, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, bgr, 2, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, zyy, 2, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, bgg, 2, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, zyz, 2, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, bgb, 2, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, zzx, 2, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, bbr, 2, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector3, zzy, 2, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, bbg, 2, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector3, zzz, 2, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector3, bbb, 2, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xxxx, 0, 0, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rrrr, 0, 0, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xxxy, 0, 0, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rrrg, 0, 0, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xxxz, 0, 0, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rrrb, 0, 0, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xxxw, 0, 0, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rrra, 0, 0, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xxyx, 0, 0, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rrgr, 0, 0, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xxyy, 0, 0, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rrgg, 0, 0, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xxyz, 0, 0, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rrgb, 0, 0, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xxyw, 0, 0, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rrga, 0, 0, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xxzx, 0, 0, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rrbr, 0, 0, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xxzy, 0, 0, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rrbg, 0, 0, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xxzz, 0, 0, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rrbb, 0, 0, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xxzw, 0, 0, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rrba, 0, 0, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xxwx, 0, 0, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rrar, 0, 0, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xxwy, 0, 0, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rrag, 0, 0, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xxwz, 0, 0, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rrab, 0, 0, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xxww, 0, 0, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rraa, 0, 0, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xyxx, 0, 1, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rgrr, 0, 1, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xyxy, 0, 1, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rgrg, 0, 1, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xyxz, 0, 1, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rgrb, 0, 1, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xyxw, 0, 1, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rgra, 0, 1, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xyyx, 0, 1, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rggr, 0, 1, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xyyy, 0, 1, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rggg, 0, 1, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xyyz, 0, 1, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rggb, 0, 1, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xyyw, 0, 1, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rgga, 0, 1, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xyzx, 0, 1, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rgbr, 0, 1, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xyzy, 0, 1, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rgbg, 0, 1, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xyzz, 0, 1, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rgbb, 0, 1, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xyzw, 0, 1, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rgba, 0, 1, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xywx, 0, 1, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rgar, 0, 1, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xywy, 0, 1, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rgag, 0, 1, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xywz, 0, 1, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rgab, 0, 1, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xyww, 0, 1, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rgaa, 0, 1, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xzxx, 0, 2, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rbrr, 0, 2, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xzxy, 0, 2, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rbrg, 0, 2, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xzxz, 0, 2, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rbrb, 0, 2, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xzxw, 0, 2, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rbra, 0, 2, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xzyx, 0, 2, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rbgr, 0, 2, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xzyy, 0, 2, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rbgg, 0, 2, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xzyz, 0, 2, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rbgb, 0, 2, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xzyw, 0, 2, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rbga, 0, 2, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xzzx, 0, 2, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rbbr, 0, 2, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xzzy, 0, 2, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rbbg, 0, 2, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xzzz, 0, 2, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rbbb, 0, 2, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xzzw, 0, 2, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rbba, 0, 2, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xzwx, 0, 2, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rbar, 0, 2, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xzwy, 0, 2, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rbag, 0, 2, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xzwz, 0, 2, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rbab, 0, 2, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xzww, 0, 2, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rbaa, 0, 2, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xwxx, 0, 3, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rarr, 0, 3, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xwxy, 0, 3, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rarg, 0, 3, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xwxz, 0, 3, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rarb, 0, 3, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xwxw, 0, 3, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rara, 0, 3, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xwyx, 0, 3, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ragr, 0, 3, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xwyy, 0, 3, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ragg, 0, 3, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xwyz, 0, 3, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ragb, 0, 3, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xwyw, 0, 3, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, raga, 0, 3, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xwzx, 0, 3, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rabr, 0, 3, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xwzy, 0, 3, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rabg, 0, 3, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xwzz, 0, 3, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, rabb, 0, 3, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xwzw, 0, 3, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, raba, 0, 3, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xwwx, 0, 3, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, raar, 0, 3, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xwwy, 0, 3, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, raag, 0, 3, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xwwz, 0, 3, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, raab, 0, 3, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, xwww, 0, 3, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, raaa, 0, 3, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yxxx, 1, 0, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, grrr, 1, 0, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yxxy, 1, 0, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, grrg, 1, 0, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yxxz, 1, 0, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, grrb, 1, 0, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yxxw, 1, 0, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, grra, 1, 0, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yxyx, 1, 0, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, grgr, 1, 0, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yxyy, 1, 0, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, grgg, 1, 0, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yxyz, 1, 0, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, grgb, 1, 0, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yxyw, 1, 0, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, grga, 1, 0, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yxzx, 1, 0, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, grbr, 1, 0, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yxzy, 1, 0, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, grbg, 1, 0, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yxzz, 1, 0, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, grbb, 1, 0, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yxzw, 1, 0, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, grba, 1, 0, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yxwx, 1, 0, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, grar, 1, 0, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yxwy, 1, 0, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, grag, 1, 0, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yxwz, 1, 0, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, grab, 1, 0, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yxww, 1, 0, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, graa, 1, 0, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yyxx, 1, 1, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ggrr, 1, 1, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yyxy, 1, 1, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ggrg, 1, 1, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yyxz, 1, 1, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ggrb, 1, 1, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yyxw, 1, 1, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ggra, 1, 1, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yyyx, 1, 1, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gggr, 1, 1, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yyyy, 1, 1, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gggg, 1, 1, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yyyz, 1, 1, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gggb, 1, 1, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yyyw, 1, 1, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ggga, 1, 1, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yyzx, 1, 1, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ggbr, 1, 1, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yyzy, 1, 1, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ggbg, 1, 1, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yyzz, 1, 1, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ggbb, 1, 1, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yyzw, 1, 1, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ggba, 1, 1, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yywx, 1, 1, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ggar, 1, 1, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yywy, 1, 1, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ggag, 1, 1, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yywz, 1, 1, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ggab, 1, 1, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yyww, 1, 1, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ggaa, 1, 1, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yzxx, 1, 2, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gbrr, 1, 2, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yzxy, 1, 2, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gbrg, 1, 2, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yzxz, 1, 2, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gbrb, 1, 2, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yzxw, 1, 2, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gbra, 1, 2, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yzyx, 1, 2, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gbgr, 1, 2, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yzyy, 1, 2, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gbgg, 1, 2, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yzyz, 1, 2, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gbgb, 1, 2, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yzyw, 1, 2, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gbga, 1, 2, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yzzx, 1, 2, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gbbr, 1, 2, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yzzy, 1, 2, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gbbg, 1, 2, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yzzz, 1, 2, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gbbb, 1, 2, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yzzw, 1, 2, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gbba, 1, 2, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yzwx, 1, 2, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gbar, 1, 2, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yzwy, 1, 2, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gbag, 1, 2, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yzwz, 1, 2, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gbab, 1, 2, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, yzww, 1, 2, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gbaa, 1, 2, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ywxx, 1, 3, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, garr, 1, 3, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ywxy, 1, 3, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, garg, 1, 3, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ywxz, 1, 3, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, garb, 1, 3, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ywxw, 1, 3, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gara, 1, 3, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ywyx, 1, 3, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gagr, 1, 3, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ywyy, 1, 3, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gagg, 1, 3, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ywyz, 1, 3, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gagb, 1, 3, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ywyw, 1, 3, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gaga, 1, 3, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ywzx, 1, 3, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gabr, 1, 3, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ywzy, 1, 3, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gabg, 1, 3, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ywzz, 1, 3, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gabb, 1, 3, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ywzw, 1, 3, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gaba, 1, 3, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ywwx, 1, 3, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gaar, 1, 3, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ywwy, 1, 3, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gaag, 1, 3, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ywwz, 1, 3, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gaab, 1, 3, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, ywww, 1, 3, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, gaaa, 1, 3, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zxxx, 2, 0, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, brrr, 2, 0, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zxxy, 2, 0, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, brrg, 2, 0, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zxxz, 2, 0, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, brrb, 2, 0, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zxxw, 2, 0, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, brra, 2, 0, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zxyx, 2, 0, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, brgr, 2, 0, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zxyy, 2, 0, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, brgg, 2, 0, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zxyz, 2, 0, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, brgb, 2, 0, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zxyw, 2, 0, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, brga, 2, 0, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zxzx, 2, 0, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, brbr, 2, 0, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zxzy, 2, 0, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, brbg, 2, 0, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zxzz, 2, 0, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, brbb, 2, 0, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zxzw, 2, 0, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, brba, 2, 0, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zxwx, 2, 0, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, brar, 2, 0, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zxwy, 2, 0, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, brag, 2, 0, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zxwz, 2, 0, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, brab, 2, 0, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zxww, 2, 0, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, braa, 2, 0, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zyxx, 2, 1, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bgrr, 2, 1, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zyxy, 2, 1, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bgrg, 2, 1, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zyxz, 2, 1, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bgrb, 2, 1, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zyxw, 2, 1, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bgra, 2, 1, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zyyx, 2, 1, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bggr, 2, 1, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zyyy, 2, 1, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bggg, 2, 1, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zyyz, 2, 1, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bggb, 2, 1, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zyyw, 2, 1, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bgga, 2, 1, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zyzx, 2, 1, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bgbr, 2, 1, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zyzy, 2, 1, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bgbg, 2, 1, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zyzz, 2, 1, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bgbb, 2, 1, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zyzw, 2, 1, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bgba, 2, 1, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zywx, 2, 1, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bgar, 2, 1, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zywy, 2, 1, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bgag, 2, 1, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zywz, 2, 1, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bgab, 2, 1, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zyww, 2, 1, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bgaa, 2, 1, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zzxx, 2, 2, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bbrr, 2, 2, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zzxy, 2, 2, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bbrg, 2, 2, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zzxz, 2, 2, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bbrb, 2, 2, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zzxw, 2, 2, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bbra, 2, 2, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zzyx, 2, 2, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bbgr, 2, 2, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zzyy, 2, 2, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bbgg, 2, 2, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zzyz, 2, 2, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bbgb, 2, 2, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zzyw, 2, 2, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bbga, 2, 2, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zzzx, 2, 2, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bbbr, 2, 2, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zzzy, 2, 2, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bbbg, 2, 2, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zzzz, 2, 2, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bbbb, 2, 2, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zzzw, 2, 2, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bbba, 2, 2, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zzwx, 2, 2, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bbar, 2, 2, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zzwy, 2, 2, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bbag, 2, 2, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zzwz, 2, 2, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bbab, 2, 2, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zzww, 2, 2, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bbaa, 2, 2, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zwxx, 2, 3, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, barr, 2, 3, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zwxy, 2, 3, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, barg, 2, 3, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zwxz, 2, 3, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, barb, 2, 3, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zwxw, 2, 3, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bara, 2, 3, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zwyx, 2, 3, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bagr, 2, 3, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zwyy, 2, 3, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bagg, 2, 3, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zwyz, 2, 3, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, bagb, 2, 3, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zwyw, 2, 3, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, baga, 2, 3, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zwzx, 2, 3, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, babr, 2, 3, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zwzy, 2, 3, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, babg, 2, 3, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zwzz, 2, 3, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, babb, 2, 3, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zwzw, 2, 3, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, baba, 2, 3, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zwwx, 2, 3, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, baar, 2, 3, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zwwy, 2, 3, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, baag, 2, 3, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zwwz, 2, 3, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, baab, 2, 3, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, zwww, 2, 3, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, baaa, 2, 3, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wxxx, 3, 0, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, arrr, 3, 0, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wxxy, 3, 0, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, arrg, 3, 0, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wxxz, 3, 0, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, arrb, 3, 0, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wxxw, 3, 0, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, arra, 3, 0, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wxyx, 3, 0, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, argr, 3, 0, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wxyy, 3, 0, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, argg, 3, 0, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wxyz, 3, 0, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, argb, 3, 0, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wxyw, 3, 0, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, arga, 3, 0, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wxzx, 3, 0, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, arbr, 3, 0, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wxzy, 3, 0, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, arbg, 3, 0, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wxzz, 3, 0, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, arbb, 3, 0, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wxzw, 3, 0, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, arba, 3, 0, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wxwx, 3, 0, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, arar, 3, 0, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wxwy, 3, 0, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, arag, 3, 0, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wxwz, 3, 0, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, arab, 3, 0, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wxww, 3, 0, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, araa, 3, 0, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wyxx, 3, 1, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, agrr, 3, 1, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wyxy, 3, 1, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, agrg, 3, 1, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wyxz, 3, 1, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, agrb, 3, 1, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wyxw, 3, 1, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, agra, 3, 1, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wyyx, 3, 1, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aggr, 3, 1, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wyyy, 3, 1, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aggg, 3, 1, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wyyz, 3, 1, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aggb, 3, 1, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wyyw, 3, 1, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, agga, 3, 1, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wyzx, 3, 1, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, agbr, 3, 1, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wyzy, 3, 1, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, agbg, 3, 1, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wyzz, 3, 1, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, agbb, 3, 1, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wyzw, 3, 1, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, agba, 3, 1, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wywx, 3, 1, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, agar, 3, 1, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wywy, 3, 1, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, agag, 3, 1, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wywz, 3, 1, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, agab, 3, 1, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wyww, 3, 1, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, agaa, 3, 1, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wzxx, 3, 2, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, abrr, 3, 2, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wzxy, 3, 2, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, abrg, 3, 2, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wzxz, 3, 2, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, abrb, 3, 2, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wzxw, 3, 2, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, abra, 3, 2, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wzyx, 3, 2, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, abgr, 3, 2, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wzyy, 3, 2, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, abgg, 3, 2, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wzyz, 3, 2, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, abgb, 3, 2, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wzyw, 3, 2, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, abga, 3, 2, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wzzx, 3, 2, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, abbr, 3, 2, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wzzy, 3, 2, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, abbg, 3, 2, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wzzz, 3, 2, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, abbb, 3, 2, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wzzw, 3, 2, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, abba, 3, 2, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wzwx, 3, 2, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, abar, 3, 2, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wzwy, 3, 2, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, abag, 3, 2, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wzwz, 3, 2, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, abab, 3, 2, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wzww, 3, 2, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, abaa, 3, 2, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wwxx, 3, 3, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aarr, 3, 3, 0, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wwxy, 3, 3, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aarg, 3, 3, 0, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wwxz, 3, 3, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aarb, 3, 3, 0, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wwxw, 3, 3, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aara, 3, 3, 0, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wwyx, 3, 3, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aagr, 3, 3, 1, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wwyy, 3, 3, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aagg, 3, 3, 1, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wwyz, 3, 3, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aagb, 3, 3, 1, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wwyw, 3, 3, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aaga, 3, 3, 1, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wwzx, 3, 3, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aabr, 3, 3, 2, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wwzy, 3, 3, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aabg, 3, 3, 2, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wwzz, 3, 3, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aabb, 3, 3, 2, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wwzw, 3, 3, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aaba, 3, 3, 2, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wwwx, 3, 3, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aaar, 3, 3, 3, 0)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wwwy, 3, 3, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aaag, 3, 3, 3, 1)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wwwz, 3, 3, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aaab, 3, 3, 3, 2)
	IKIGAI_VECTOR_SWIZZLE(Vector4, wwww, 3, 3, 3, 3)
	IKIGAI_VECTOR_SWIZZLE(Vector4, aaaa, 3, 3, 3, 3)
#undef IKIGAI_VECTOR_SWIZZLE
	std::array<T, 4> toArray() const {
		return { x, y, z, w };
	}
	void fromArray(const std::array<T, 4>& values) {
		x = values[0];
		y = values[1];
		z = values[2];
		w = values[3];
	}

	Vector4()
		: x(0)
		, y(0)
		, z(0)
		, w(0) { };
	Vector4(const T& _v)
		: x(_v)
		, y(_v)
		, z(_v)
		, w(_v) { };
	Vector4(const T& _x, const T& _y, const T& _z, const T& _w)
		: x(_x)
		, y(_y)
		, z(_z)
		, w(_w) { };
	Vector4(const Vector3<T>& v, const T& _w)
		: x(v.x)
		, y(v.y)
		, z(v.z)
		, w(_w) { };

	static const Vector4 One;
	static const Vector4 Zero;

	T getX() {
		return x;
	}
	T getY() {
		return y;
	}
	T getZ() {
		return z;
	}
	T getW() {
		return w;
	}

	void setX(T v) {
		x = v;
	}
	void setY(T v) {
		y = v;
	}
	void setZ(T v) {
		z = v;
	}
	void setW(T v) {
		w = v;
	}

	Vector4& operator=(const Vector4& right) {
		if (this == &right) {
			return *this;
		}
		x = right.x;
		y = right.y;
		z = right.z;
		w = right.w;
		return *this;
	}

	float& operator[](const unsigned index) {
		switch (index) {
		case 0:
			return x;
		case 1:
			return y;
		case 2:
			return z;
		case 3:
			return w;
		default:
			break;
		}
		throw std::out_of_range("Vector2: out of range");
	}

	const float& operator[](const unsigned index) const {
		switch (index) {
		case 0:
			return x;
		case 1:
			return y;
		case 2:
			return z;
		case 3:
			return w;
		default:
			break;
		}
		throw std::out_of_range("Vector2: out of range");
	}

	float dot(const Vector4& V) {
		return Dot(*this, V);
	}

	float length() {
		return Length(*this);
	}

	Vector4 normalize() {
		return Normalize(*this);
	}

	static float Dot(const Vector4& V1, const Vector4& V2) {
		return V1.x * V2.x + V1.y * V2.y + V1.z * V2.z + V1.w * V2.w;
	}

	static float Length(const Vector4& V) {
		return sqrt(pow(V.x, 2) + pow(V.y, 2) + pow(V.z, 2) + pow(V.w, 2));
	}

	static Vector4 Normalize(const Vector4& target) {
		const float length = Length(target);
		// Match Vector3: a zero vector has no direction and stays zero.
		return length > 0.0f ? target / length : Zero;
	}

	Vector4 operator-() {
		return Vector4(-x, -y, -z, -w);
	}

	void operator+=(const Vector4& V2) {
		x += V2.x;
		y += V2.y;
		z += V2.z;
		w += V2.w;
	}

	void operator-=(const Vector4& V2) {
		x -= V2.x;
		y -= V2.y;
		z -= V2.z;
		w -= V2.w;
	}

	Vector4 operator+(const Vector4& V2) const {
		if constexpr (std::is_same_v<T, float>) {
#ifdef IKIGAI_USE_SSE
			std::array<float, 4> result {};
			__m128 a = _mm_loadu_ps(toArray().data());
			__m128 b = _mm_loadu_ps(V2.toArray().data());
			_mm_storeu_ps(result.data(), _mm_add_ps(a, b));
			return Vector4(result[0], result[1], result[2], result[3]);
#elif defined(IKIGAI_USE_NEON)
			std::array<float, 4> result {};
			float32x4_t a = vld1q_f32(toArray().data());
			float32x4_t b = vld1q_f32(V2.toArray().data());
			vst1q_f32(result.data(), vaddq_f32(a, b));
			return Vector4(result[0], result[1], result[2], result[3]);
#else
			return Vector4(x + V2.x, y + V2.y, z + V2.z, w + V2.w);
#endif
		}
		else {
			return Vector4(x + V2.x, y + V2.y, z + V2.z, w + V2.w);
		}
	}

	Vector4 operator-(const Vector4& V2) const {
		if constexpr (std::is_same_v<T, float>) {
#ifdef IKIGAI_USE_SSE
			std::array<float, 4> result {};
			__m128 a = _mm_loadu_ps(toArray().data());
			__m128 b = _mm_loadu_ps(V2.toArray().data());
			_mm_storeu_ps(result.data(), _mm_sub_ps(a, b));
			return Vector4(result[0], result[1], result[2], result[3]);
#elif defined(IKIGAI_USE_NEON)
			std::array<float, 4> result {};
			float32x4_t a = vld1q_f32(toArray().data());
			float32x4_t b = vld1q_f32(V2.toArray().data());
			vst1q_f32(result.data(), vsubq_f32(a, b));
			return Vector4(result[0], result[1], result[2], result[3]);
#else
			return Vector4(x - V2.x, y - V2.y, z - V2.z, w - V2.w);
#endif
		}
		else {
			return Vector4(x - V2.x, y - V2.y, z - V2.z, w - V2.w);
		}
	}

	Vector4 operator*(const Vector4& V2) const {
		if constexpr (std::is_same_v<T, float>) {
#ifdef IKIGAI_USE_SSE
			std::array<float, 4> result {};
			__m128 a = _mm_loadu_ps(toArray().data());
			__m128 b = _mm_loadu_ps(V2.toArray().data());
			_mm_storeu_ps(result.data(), _mm_mul_ps(a, b));
			return Vector4(result[0], result[1], result[2], result[3]);
#elif defined(IKIGAI_USE_NEON)
			std::array<float, 4> result {};
			float32x4_t a = vld1q_f32(toArray().data());
			float32x4_t b = vld1q_f32(V2.toArray().data());
			vst1q_f32(result.data(), vmulq_f32(a, b));
			return Vector4(result[0], result[1], result[2], result[3]);
#else
			return Vector4(x * V2.x, y * V2.y, z * V2.z, w * V2.w);
#endif
		}
		else {
			return Vector4(x * V2.x, y * V2.y, z * V2.z, w * V2.w);
		}
	}

	Vector4 operator*(const T f) const {
		if constexpr (std::is_same_v<T, float>) {
#ifdef IKIGAI_USE_SSE
			std::array<float, 4> result {};
			__m128 a = _mm_loadu_ps(toArray().data());
			__m128 b = _mm_set1_ps(f);
			_mm_storeu_ps(result.data(), _mm_mul_ps(a, b));
			return Vector4(result[0], result[1], result[2], result[3]);
#elif defined(IKIGAI_USE_NEON)
			std::array<float, 4> result {};
			float32x4_t a = vld1q_f32(toArray().data());
			float32x4_t b = vdupq_n_f32(f);
			vst1q_f32(result.data(), vmulq_f32(a, b));
			return Vector4(result[0], result[1], result[2], result[3]);
#else
			return Vector4(x * f, y * f, z * f, w * f);
#endif
		}
		else {
			return Vector4(x * f, y * f, z * f, w * f);
		}
	}

	Vector4 operator/(const T f) const {
		if constexpr (std::is_same_v<T, float>) {
#ifdef IKIGAI_USE_SSE
			std::array<float, 4> result {};
			__m128 a = _mm_loadu_ps(toArray().data());
			__m128 b = _mm_set1_ps(f);
			_mm_storeu_ps(result.data(), _mm_div_ps(a, b));
			return Vector4(result[0], result[1], result[2], result[3]);
#elif defined(IKIGAI_USE_NEON)
			std::array<float, 4> result {};
			float32x4_t a = vld1q_f32(toArray().data());
			float32x4_t b = vdupq_n_f32(1.0f / f);
			vst1q_f32(result.data(), vmulq_f32(a, b)); // NEON usually doesn't have a direct float division instruction, so multiply by reciprocal
			return Vector4(result[0], result[1], result[2], result[3]);
#else
			return Vector4(x / f, y / f, z / f, w / f);
#endif
		}
		else {
			return Vector4(x / f, y / f, z / f, w / f);
		}
	}

	template <class Context> constexpr static void serde(Context& context, Vector4& value);
};

template <class T> Vector4<T> operator*(const float f, const Vector4<T>& V) {
	return V * f;
}
template <class T> Vector4<T> operator/(const float f, const Vector4<T>& V) {
	return V / f;
}
template <class T> bool operator==(const Vector4<T>& left, const Vector4<T>& right) {
	return left.x == right.x && left.y == right.y && left.z == right.z && left.w == right.w;
}
template <class T> bool operator!=(const Vector4<T>& left, const Vector4<T>& right) {
	return !operator==(left, right);
}

template <class T> struct Matrix3 {
private:
	std::array<T, 9> data = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

public:
	static const Matrix3 Identity;
	static const Matrix3 Zero;

	Matrix3() {
		data = Identity.data;
	}

	Matrix3(T val) {
		for (auto i = 0u; i < data.size(); i++) {
			data[i] = val;
		}
	}

	T* getData() {
		return data.data();
	}

	const T* getData() const {
		return data.data();
	}

	Matrix3(T element1, T element2, T element3, T element4, T element5, T element6, T element7, T element8, T element9) {
		data[0] = element1;
		data[1] = element2;
		data[2] = element3;
		data[3] = element4;
		data[4] = element5;
		data[5] = element6;
		data[6] = element7;
		data[7] = element8;
		data[8] = element9;
	}

	template <class U> Matrix3(const Vector3<U>& a, const Vector3<U>& b, const Vector3<U>& c) {
		data[0] = a.x;
		data[1] = a.y;
		data[2] = a.z;
		data[3] = b.x;
		data[4] = b.y;
		data[5] = b.z;
		data[6] = c.x;
		data[7] = c.y;
		data[8] = c.z;
	}

	Matrix3(std::array<T, 9>&& in) {
		data = std::move(in);
	};

	Matrix3(const Matrix3& in) {
		*this = in;
	}

	Matrix3(Matrix3&& in) noexcept {
		data = std::move(in.data);
	}

	Vector3<T> transform(const Vector3<T>& vector) const {
		return (*this) * vector;
	}

	void setInertiaTensorCoeffs(T ix, T iy, T iz, T ixy, T ixz, T iyz) {
		data[0] = ix;
		data[1] = data[3] = -ixy;
		data[2] = data[6] = -ixz;
		data[4] = iy;
		data[5] = data[7] = -iyz;
		data[8] = iz;
	}

	void setBlockInertiaTensor(const Vector3<T>& halfSizes, float mass) {
		Vector3 squares = halfSizes.componentProduct(halfSizes);
		setInertiaTensorCoeffs(0.3f * mass * (squares.y + squares.z), 0.3f * mass * (squares.x + squares.z), 0.3f * mass * (squares.x + squares.y));
	}

	void setInverse(const Matrix3& m) {
		float t4 = m.data[0] * m.data[4];
		float t6 = m.data[0] * m.data[5];
		float t8 = m.data[1] * m.data[3];
		float t10 = m.data[2] * m.data[3];
		float t12 = m.data[1] * m.data[6];
		float t14 = m.data[2] * m.data[6];

		// Calculate the determinant
		float t16 = (t4 * m.data[8] - t6 * m.data[7] - t8 * m.data[8] + t10 * m.data[7] + t12 * m.data[5] - t14 * m.data[4]);

		// Make sure the determinant is non-zero.
		if (t16 == 0.0f) {
			return;
		}
		float t17 = 1 / t16;

		data[0] = (m.data[4] * m.data[8] - m.data[5] * m.data[7]) * t17;
		data[1] = -(m.data[1] * m.data[8] - m.data[2] * m.data[7]) * t17;
		data[2] = (m.data[1] * m.data[5] - m.data[2] * m.data[4]) * t17;
		data[3] = -(m.data[3] * m.data[8] - m.data[5] * m.data[6]) * t17;
		data[4] = (m.data[0] * m.data[8] - t14) * t17;
		data[5] = -(t6 - t10) * t17;
		data[6] = (m.data[3] * m.data[7] - m.data[4] * m.data[6]) * t17;
		data[7] = -(m.data[0] * m.data[7] - t12) * t17;
		data[8] = (t4 - t8) * t17;
	}

	void setComponents(const Vector3<T>& compOne, const Vector3<T>& compTwo, const Vector3<T>& compThree) {
		data[0] = compOne.x;
		data[1] = compTwo.x;
		data[2] = compThree.x;
		data[3] = compOne.y;
		data[4] = compTwo.y;
		data[5] = compThree.y;
		data[6] = compOne.z;
		data[7] = compTwo.z;
		data[8] = compThree.z;
	}

	Vector3<T> transformTranspose(const Vector3<T>& vector) const {
		return Vector3(vector.x * data[0] + vector.y * data[3] + vector.z * data[6], vector.x * data[1] + vector.y * data[4] + vector.z * data[7],
			vector.x * data[2] + vector.y * data[5] + vector.z * data[8]);
	}

	void setSkewSymmetric(const Vector3<T> vector) {
		data[0] = data[4] = data[8] = 0;
		data[1] = -vector.z;
		data[2] = vector.y;
		data[3] = vector.z;
		data[5] = -vector.x;
		data[6] = -vector.y;
		data[7] = vector.x;
	}

	Matrix3 transpose() const {
		return Transpose(*this);
	}

	Matrix3 inverse() const {
		return Inverse(*this);
	}
	static Matrix3 LinearInterpolate(const Matrix3& a, const Matrix3& b, float prop) {
		Matrix3 result;
		for (unsigned i = 0; i < 9; i++) {
			result.data[i] = a.data[i] * (1 - prop) + b.data[i] * prop;
		}
		return result;
	}

	Matrix3& operator=(const Matrix3& other) {
		this->data = other.data;
		return *this;
	}

	bool operator==(const Matrix3& other) {
		return AreEquals(*this, other);
	}

	Matrix3 operator+(const Matrix3& other) const {
		return Add(*this, other);
	}

	Matrix3& operator+=(const Matrix3& other) {
		*this = Add(*this, other);
		return *this;
	}

	Matrix3 operator-(const Matrix3& other) const {
		return Subtract(*this, other);
	}

	Matrix3& operator-=(const Matrix3& other) {
		*this = Subtract(*this, other);
		return *this;
	}

	Matrix3 operator*(T scalar) const {
		return Multiply(*this, scalar);
	}

	Matrix3& operator*=(T scalar) {
		*this = Multiply(*this, scalar);
		return *this;
	}

	Vector3<T> operator*(const Vector3<T>& vector) const {
		return Multiply(*this, vector);
	}

	Matrix3 operator*(const Matrix3& other) const {
		return Multiply(*this, other);
	}

	Matrix3& operator*=(const Matrix3& other) {
		*this = Multiply(*this, other);
		return *this;
	}

	Matrix3 operator/(T scalar) const {
		return Divide(*this, scalar);
	}

	Matrix3& operator/=(T scalar) {
		*this = Divide(*this, scalar);
		return *this;
	}

	Matrix3 operator/(const Matrix3& other) const {
		return Divide(*this, other);
	}

	Matrix3& operator/=(const Matrix3& other) {
		*this = Divide(*this, other);
		return *this;
	}

	float& operator()(unsigned row, unsigned column) {
		if (row >= 3 || column >= 3) {
			throw std::out_of_range("Invalid index : " + std::to_string(row) + "," + std::to_string(column) + " is out of range");
		}
		return data[3 * row + column];
	}

	static bool AreEquals(const Matrix3& left, const Matrix3& right) {
		return left.data == right.data;
	}

	static Matrix3 Add(const Matrix3& left, T scalar) {
		Matrix3 result(left);
		for (unsigned i = 0; i < 9; ++i) {
			result.data[i] += scalar;
		}
		return result;
	}

	static Matrix3 Add(const Matrix3& left, const Matrix3& right) {
		Matrix3 result(left);
		for (unsigned i = 0; i < 9; ++i) {
			result.data[i] += right.data[i];
		}
		return result;
	}

	static Matrix3 Subtract(const Matrix3& left, T scalar) {
		Matrix3 result(left);
		for (unsigned i = 0; i < 9; ++i) {
			result.data[i] -= scalar;
		}
		return result;
	}

	static Matrix3 Subtract(const Matrix3& left, const Matrix3& right) {
		Matrix3 result(left);
		for (unsigned i = 0; i < 9; ++i) {
			result.data[i] -= right.data[i];
		}
		return result;
	}

	static Matrix3 Multiply(const Matrix3& left, T scalar) {
		Matrix3 result(left);
		for (unsigned i = 0; i < 9; ++i) {
			result.data[i] *= scalar;
		}
		return result;
	}

	static Vector3<T> Multiply(const Matrix3& matrix, const Vector3<T>& vector) {
		Vector3<T> result;
		result.x = ((matrix.data[0] * vector.x) + (matrix.data[1] * vector.y) + (matrix.data[2] * vector.z));
		result.y = ((matrix.data[3] * vector.x) + (matrix.data[4] * vector.y) + (matrix.data[5] * vector.z));
		result.z = ((matrix.data[6] * vector.x) + (matrix.data[7] * vector.y) + (matrix.data[8] * vector.z));
		return result;
	}

	static Matrix3 Multiply(const Matrix3& left, const Matrix3& right) {
		return Matrix3((left.data[0] * right.data[0]) + (left.data[1] * right.data[3]) + (left.data[2] * right.data[6]),
			(left.data[0] * right.data[1]) + (left.data[1] * right.data[4]) + (left.data[2] * right.data[7]),
			(left.data[0] * right.data[2]) + (left.data[1] * right.data[5]) + (left.data[2] * right.data[8]),

			(left.data[3] * right.data[0]) + (left.data[4] * right.data[3]) + (left.data[5] * right.data[6]),
			(left.data[3] * right.data[1]) + (left.data[4] * right.data[4]) + (left.data[5] * right.data[7]),
			(left.data[3] * right.data[2]) + (left.data[4] * right.data[5]) + (left.data[5] * right.data[8]),

			(left.data[6] * right.data[0]) + (left.data[7] * right.data[3]) + (left.data[8] * right.data[6]),
			(left.data[6] * right.data[1]) + (left.data[7] * right.data[4]) + (left.data[8] * right.data[7]),
			(left.data[6] * right.data[2]) + (left.data[7] * right.data[5]) + (left.data[8] * right.data[8]));
	}

	static Matrix3 Divide(const Matrix3& left, T scalar) {
		Matrix3 result(left);
		for (unsigned i = 0; i < 9; ++i) {
			result.data[i] /= scalar;
		}
		return result;
	}

	static Matrix3 Divide(const Matrix3& left, const Matrix3& right) {
		return left * Inverse(right);
	}

	static bool IsIdentity(const Matrix3& matrix) {
		return Identity.data == matrix.data;
	}

	static float Determinant(const Matrix3& matrix) {
		return matrix.data[0] * (matrix.data[4] * matrix.data[8] - matrix.data[5] * matrix.data[7])
			- matrix.data[3] * (matrix.data[1] * matrix.data[8] - matrix.data[2] * matrix.data[7])
			+ matrix.data[6] * (matrix.data[1] * matrix.data[5] - matrix.data[2] * matrix.data[4]);
	}

	static Matrix3 Transpose(const Matrix3& matrix) {
		Matrix3 result;

		result.data[0] = matrix.data[0];
		result.data[1] = matrix.data[3];
		result.data[2] = matrix.data[6];

		result.data[3] = matrix.data[1];
		result.data[4] = matrix.data[4];
		result.data[5] = matrix.data[7];

		result.data[6] = matrix.data[2];
		result.data[7] = matrix.data[5];
		result.data[8] = matrix.data[8];

		return result;
	}

	static Matrix3 Cofactor(const Matrix3& matrix) {
		return Matrix3(((matrix.data[4] * matrix.data[8]) - (matrix.data[5] * matrix.data[7])), // 0
			-((matrix.data[3] * matrix.data[8]) - (matrix.data[5] * matrix.data[6])), // 1
			((matrix.data[3] * matrix.data[7]) - (matrix.data[4] * matrix.data[6])), // 2
			-((matrix.data[1] * matrix.data[8]) - (matrix.data[2] * matrix.data[7])), // 3
			((matrix.data[0] * matrix.data[8]) - (matrix.data[2] * matrix.data[6])), // 4
			-((matrix.data[0] * matrix.data[7]) - (matrix.data[1] * matrix.data[6])), // 5
			((matrix.data[1] * matrix.data[5]) - (matrix.data[2] * matrix.data[4])), // 6
			-((matrix.data[0] * matrix.data[5]) - (matrix.data[2] * matrix.data[3])), // 7
			((matrix.data[0] * matrix.data[4]) - (matrix.data[1] * matrix.data[3]))); // 8
	}

	static Matrix3 Minor(const Matrix3& matrix) {
		return Matrix3(((matrix.data[4] * matrix.data[8]) - (matrix.data[5] * matrix.data[7])), // 0
			((matrix.data[3] * matrix.data[8]) - (matrix.data[5] * matrix.data[6])), // 1
			((matrix.data[3] * matrix.data[7]) - (matrix.data[4] * matrix.data[6])), // 2
			((matrix.data[1] * matrix.data[8]) - (matrix.data[2] * matrix.data[7])), // 3
			((matrix.data[0] * matrix.data[8]) - (matrix.data[2] * matrix.data[6])), // 4
			((matrix.data[0] * matrix.data[7]) - (matrix.data[1] * matrix.data[6])), // 5
			((matrix.data[1] * matrix.data[5]) - (matrix.data[2] * matrix.data[4])), // 6
			((matrix.data[0] * matrix.data[5]) - (matrix.data[2] * matrix.data[3])), // 7
			((matrix.data[0] * matrix.data[4]) - (matrix.data[1] * matrix.data[3]))); // 8
	}

	static Matrix3 Adjoint(const Matrix3& other) {
		return Transpose(Cofactor(other));
	}

	static Matrix3 Inverse(const Matrix3& matrix) {
		const float determinant = Determinant(matrix);
		if (determinant == 0) {
			throw std::logic_error("Division by 0");
		}

		return Adjoint(matrix) / determinant;
	}

	static Matrix3 Rotation(float rotation) {
		return Matrix3(std::cos(rotation), -std::sin(rotation), 0, std::sin(rotation), std::cos(rotation), 0, 0, 0, 1);
	}

	static Matrix3 Rotation(const Matrix3& matrix, float rotation) {
		return matrix * Rotation(rotation);
	}

	static Vector3<T> GetRow(const Matrix3& matrix, unsigned row) {
		if (row >= 3) {
			throw std::out_of_range("Invalid index : " + std::to_string(row) + " is out of range");
		}

		return Vector3(matrix.data[row * 3], matrix.data[row * 3 + 1], matrix.data[row * 3 + 2]);
	}

	static Vector3<T> GetColumn(const Matrix3& matrix, unsigned column) {
		if (column >= 3) {
			throw std::out_of_range("Invalid index : " + std::to_string(column) + " is out of range");
		}

		return Vector3(matrix.data[column], matrix.data[column + 3], matrix.data[column + 6]);
	}

	template <class Context> constexpr static void serde(Context& context, Matrix3& value);
};
template <class T> Matrix3<T> operator*(const float f, const Matrix3<T>& V) {
	return V * f;
}
template <class T> Matrix3<T> operator/(const float f, const Matrix3<T>& V) {
	return V / f;
}

template <class T> struct Matrix4 {
private:
	std::array<T, 16> data = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

public:
	Matrix4() {
		data = Identity.data;
	}
	Matrix4(T val) {
		data[0] = data[5] = data[10] = data[15] = val;
	}

	Matrix4(std::array<T, 16>&& in) {
		data = in;
	};

	Matrix4(T element1, T element2, T element3, T element4, T element5, T element6, T element7, T element8, T element9, T element10, T element11,
		T element12, T element13, T element14, T element15, T element16) {
		data[0] = element1;
		data[1] = element2;
		data[2] = element3;
		data[3] = element4;
		data[4] = element5;
		data[5] = element6;
		data[6] = element7;
		data[7] = element8;
		data[8] = element9;
		data[9] = element10;
		data[10] = element11;
		data[11] = element12;
		data[12] = element13;
		data[13] = element14;
		data[14] = element15;
		data[15] = element16;
	}

	Matrix4(const Matrix4& in) {
		*this = in;
	}

	Matrix4(Matrix4&& in) noexcept {
		data = std::move(in.data);
	}

	Matrix4 MakeBiasMatrix() {
		Matrix4 Result(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.5f, 0.5f, 1.0f);
		return Result;
	}

	T* getData() {
		return data.data();
	}

	const T* getData() const {
		return data.data();
	}

	Vector3<T> transformInverseDirection(const Vector3<T>& vector) const {
		return Vector3(vector.x * data[0] + vector.y * data[4] + vector.z * data[8],

			vector.x * data[1] + vector.y * data[5] + vector.z * data[9],

			vector.x * data[2] + vector.y * data[6] + vector.z * data[10]);
	}

	Vector3<T> transform(const Vector3<T>& vector) const {
		return Vector3(vector.x * data[0] + vector.y * data[1] + vector.z * data[2] + data[3],

			vector.x * data[4] + vector.y * data[5] + vector.z * data[6] + data[7],

			vector.x * data[8] + vector.y * data[9] + vector.z * data[10] + data[11]);
	}

	Vector3<T> transformInverse(const Vector3<T>& vector) const {
		Vector3 tmp = vector;
		tmp.x -= data[3];
		tmp.y -= data[7];
		tmp.z -= data[11];
		return Vector3(tmp.x * data[0] + tmp.y * data[4] + tmp.z * data[8],

			tmp.x * data[1] + tmp.y * data[5] + tmp.z * data[9],

			tmp.x * data[2] + tmp.y * data[6] + tmp.z * data[10]);
	}

	Vector3<T> transformDirection(const Vector3<T>& vector) const {
		return Vector3(vector.x * data[0] + vector.y * data[1] + vector.z * data[2],

			vector.x * data[4] + vector.y * data[5] + vector.z * data[6],

			vector.x * data[8] + vector.y * data[9] + vector.z * data[10]);
	}

	Vector3<T> getAxisVector(int i) const {
		return Vector3(data[i], data[i + 4], data[i + 8]);
	}

	Vector3<T> operator*(const Vector3<T>& vector) const {
		return Vector3(vector.x * data[0] + vector.y * data[1] + vector.z * data[2] + data[3],

			vector.x * data[4] + vector.y * data[5] + vector.z * data[6] + data[7],

			vector.x * data[8] + vector.y * data[9] + vector.z * data[10] + data[11]);
	}

	static const Matrix4 Identity;
	static const Matrix4 Zero;

	Matrix4& operator=(const Matrix4& other) {
		this->data = other.data;
		return *this;
	}

	bool operator==(const Matrix4& other) {
		return AreEquals(*this, other);
	}

	Matrix4 operator+(const Matrix4& other) const {
		return Add(*this, other);
	}

	Matrix4& operator+=(const Matrix4& other) {
		*this = Add(*this, other);
		return *this;
	}

	Matrix4 operator-(T scalar) const {
		return Subtract(*this, scalar);
	}

	Matrix4 operator-(const Matrix4& other) const {
		return Subtract(*this, other);
	}

	Matrix4& operator-=(const Matrix4& other) {
		*this = Subtract(*this, other);
		return *this;
	}

	Matrix4 operator*(T scalar) const {
		return Multiply(*this, scalar);
	}

	Matrix4& operator*=(T scalar) {
		*this = Multiply(*this, scalar);
		return *this;
	}

	Vector4<T> operator*(const Vector4<T>& vector) const {
		return Multiply(*this, vector);
	}

	Matrix4 operator*(const Matrix4& other) const {
		return Multiply(*this, other);
	}

	Matrix4& operator*=(const Matrix4& other) {
		*this = Multiply(*this, other);
		return *this;
	}

	Matrix4 operator/(T scalar) const {
		return Divide(*this, scalar);
	}

	Matrix4& operator/=(T scalar) {
		*this = Divide(*this, scalar);
		return *this;
	}

	Matrix4 operator/(const Matrix4& other) const {
		return Divide(*this, other);
	}

	Matrix4& operator/=(const Matrix4& other) {
		*this = Divide(*this, other);
		return *this;
	}
	float& operator()(uint8_t row, uint8_t column) {
		if (row >= 4 || column >= 4) {
			throw std::out_of_range("Invalid index : " + std::to_string(row) + "," + std::to_string(column) + " is out of range");
		}
		return data[4 * row + column];
	}

	void setRow(uint8_t row, const Vector4<T>& vec) {
		if (row >= 4) {
			throw std::out_of_range("Invalid index : " + std::to_string(row) + " is out of range");
		}

		data[4 * row] = vec.x;
		data[4 * row + 1] = vec.y;
		data[4 * row + 2] = vec.z;
		data[4 * row + 3] = vec.w;
	}

	void setCol(uint8_t col, const Vector4<T>& vec) {
		if (col >= 4) {
			throw std::out_of_range("Invalid index : " + std::to_string(col) + " is out of range");
		}

		data[col] = vec.x;
		data[col + 4] = vec.y;
		data[col + 8] = vec.z;
		data[col + 12] = vec.w;
	}

	Vector4<T> getRow(uint8_t row) {
		if (row >= 4) {
			throw std::out_of_range("Invalid index : " + std::to_string(row) + " is out of range");
		}

		return Vector4(data[4 * row], data[4 * row + 1], data[4 * row + 2], data[4 * row + 3]);
	}

	Vector4<T> getCol(uint8_t col) {
		if (col >= 4) {
			throw std::out_of_range("Invalid index : " + std::to_string(col) + " is out of range");
		}

		return Vector4(data[col], data[col + 4], data[col + 8], data[col + 12]);
	}

	static Matrix4 Scaling(const Vector3<T>& scale) {
		return Matrix4(scale.x, 0, 0, 0, 0, scale.y, 0, 0, 0, 0, scale.z, 0, 0, 0, 0, 1);
	}

	static Matrix4 Translation(const Vector3<T>& translation) {
		return Matrix4(1, 0, 0, translation.x, 0, 1, 0, translation.y, 0, 0, 1, translation.z, 0, 0, 0, 1);
	}

	static Matrix4 Rotation(T angle, const Vector3<T>& v) {
		const T c = cos(angle);
		const T s = sin(angle);

		const Vector3<T> axis = Vector3<T>::Normalize(v);
		const Vector3<T> temp((T(1) - c) * axis);

		// Right-handed active rotation of column vectors; storage is row-major.
		// Строка 0
		const T r00 = c + temp.x * axis.x;
		const T r01 = temp.x * axis.y - s * axis.z;
		const T r02 = temp.x * axis.z + s * axis.y;
		// Строка 1
		const T r10 = temp.y * axis.x + s * axis.z;
		const T r11 = c + temp.y * axis.y;
		const T r12 = temp.y * axis.z - s * axis.x;
		// Строка 2
		const T r20 = temp.z * axis.x - s * axis.y;
		const T r21 = temp.z * axis.y + s * axis.x;
		const T r22 = c + temp.z * axis.z;

		// Результат = Identity * rotate → просто заполняем верхний левый 3x3 блок
		Matrix4<T> res = Identity;
		res(0, 0) = r00;
		res(0, 1) = r01;
		res(0, 2) = r02;
		res(1, 0) = r10;
		res(1, 1) = r11;
		res(1, 2) = r12;
		res(2, 0) = r20;
		res(2, 1) = r21;
		res(2, 2) = r22;
		return res;
	}

	static Matrix4 Rotation(const Matrix4& matrix, float rotation, const Vector3<T>& axis) {
		return matrix * Rotation(rotation, axis);
	}

	static bool AreEquals(const Matrix4& left, const Matrix4& right) {
		return left.data == right.data;
	}

	static Matrix4 Add(const Matrix4& left, T scalar) {
		Matrix4 result(left);
		for (unsigned i = 0; i < 16; i++) {
			result.data[i] += scalar;
		}
		return result;
	}

	static Matrix4 Add(const Matrix4& left, const Matrix4& right) {
		Matrix4 result(left);
		for (unsigned i = 0; i < 16; i++) {
			result.data[i] += right.data[i];
		}
		return result;
	}

	static Matrix4 Subtract(const Matrix4& left, T scalar) {
		Matrix4 result(left);
		for (unsigned i = 0; i < 16; ++i) {
			result.data[i] -= scalar;
		}
		return result;
	}

	static Matrix4 Subtract(const Matrix4& left, const Matrix4& right) {
		Matrix4 result(left);
		for (unsigned i = 0; i < 16; ++i) {
			result.data[i] -= right.data[i];
		}
		return result;
	}

	static Matrix4 Multiply(const Matrix4& left, T scalar) {
		Matrix4 result(left);
		for (unsigned i = 0; i < 16; ++i) {
			result.data[i] *= scalar;
		}
		return result;
	}

	static Vector4<T> Multiply(const Matrix4& matrix, const Vector4<T>& vector) {
		Vector4<T> multiply;

		multiply.x = ((matrix.data[0] * vector.x) + (matrix.data[1] * vector.y) + (matrix.data[2] * vector.z) + (matrix.data[3] * vector.w));
		multiply.y = ((matrix.data[4] * vector.x) + (matrix.data[5] * vector.y) + (matrix.data[6] * vector.z) + (matrix.data[7] * vector.w));
		multiply.z = ((matrix.data[8] * vector.x) + (matrix.data[9] * vector.y) + (matrix.data[10] * vector.z) + (matrix.data[11] * vector.w));
		multiply.w = ((matrix.data[12] * vector.x) + (matrix.data[13] * vector.y) + (matrix.data[14] * vector.z) + (matrix.data[15] * vector.w));
		return multiply;
	}

	static Matrix4 Multiply(const Matrix4& left, const Matrix4& right) {
		if constexpr (std::is_same_v<T, float>) {
#ifdef IKIGAI_USE_SSE
			Matrix4 result;
			const float* A = left.data.data();
			const float* B = right.data.data();
			float* C = result.data.data();

			__m128 b0 = _mm_loadu_ps(B);
			__m128 b1 = _mm_loadu_ps(B + 4);
			__m128 b2 = _mm_loadu_ps(B + 8);
			__m128 b3 = _mm_loadu_ps(B + 12);

			for (int i = 0; i < 4; ++i) {
				__m128 a0 = _mm_set1_ps(A[i * 4 + 0]);
				__m128 a1 = _mm_set1_ps(A[i * 4 + 1]);
				__m128 a2 = _mm_set1_ps(A[i * 4 + 2]);
				__m128 a3 = _mm_set1_ps(A[i * 4 + 3]);

				__m128 r = _mm_add_ps(_mm_mul_ps(a0, b0), _mm_add_ps(_mm_mul_ps(a1, b1), _mm_add_ps(_mm_mul_ps(a2, b2), _mm_mul_ps(a3, b3))));
				_mm_storeu_ps(C + i * 4, r);
			}
			return result;
#elif defined(IKIGAI_USE_NEON)
			Matrix4 result;
			const float* A = left.data.data();
			const float* B = right.data.data();
			float* C = result.data.data();

			float32x4_t b0 = vld1q_f32(B);
			float32x4_t b1 = vld1q_f32(B + 4);
			float32x4_t b2 = vld1q_f32(B + 8);
			float32x4_t b3 = vld1q_f32(B + 12);

			for (int i = 0; i < 4; ++i) {
				float32x4_t a0 = vdupq_n_f32(A[i * 4 + 0]);
				float32x4_t a1 = vdupq_n_f32(A[i * 4 + 1]);
				float32x4_t a2 = vdupq_n_f32(A[i * 4 + 2]);
				float32x4_t a3 = vdupq_n_f32(A[i * 4 + 3]);

				float32x4_t r = vaddq_f32(vmulq_f32(a0, b0), vaddq_f32(vmulq_f32(a1, b1), vaddq_f32(vmulq_f32(a2, b2), vmulq_f32(a3, b3))));
				vst1q_f32(C + i * 4, r);
			}
			return result;
#endif
		}
		return Matrix4(
			((left.data[0] * right.data[0]) + (left.data[1] * right.data[4]) + (left.data[2] * right.data[8]) + (left.data[3] * right.data[12])),
			((left.data[0] * right.data[1]) + (left.data[1] * right.data[5]) + (left.data[2] * right.data[9]) + (left.data[3] * right.data[13])),
			((left.data[0] * right.data[2]) + (left.data[1] * right.data[6]) + (left.data[2] * right.data[10]) + (left.data[3] * right.data[14])),
			((left.data[0] * right.data[3]) + (left.data[1] * right.data[7]) + (left.data[2] * right.data[11]) + (left.data[3] * right.data[15])),
			((left.data[4] * right.data[0]) + (left.data[5] * right.data[4]) + (left.data[6] * right.data[8]) + (left.data[7] * right.data[12])),
			((left.data[4] * right.data[1]) + (left.data[5] * right.data[5]) + (left.data[6] * right.data[9]) + (left.data[7] * right.data[13])),
			((left.data[4] * right.data[2]) + (left.data[5] * right.data[6]) + (left.data[6] * right.data[10]) + (left.data[7] * right.data[14])),
			((left.data[4] * right.data[3]) + (left.data[5] * right.data[7]) + (left.data[6] * right.data[11]) + (left.data[7] * right.data[15])),
			((left.data[8] * right.data[0]) + (left.data[9] * right.data[4]) + (left.data[10] * right.data[8]) + (left.data[11] * right.data[12])),
			((left.data[8] * right.data[1]) + (left.data[9] * right.data[5]) + (left.data[10] * right.data[9]) + (left.data[11] * right.data[13])),
			((left.data[8] * right.data[2]) + (left.data[9] * right.data[6]) + (left.data[10] * right.data[10]) + (left.data[11] * right.data[14])),
			((left.data[8] * right.data[3]) + (left.data[9] * right.data[7]) + (left.data[10] * right.data[11]) + (left.data[11] * right.data[15])),
			((left.data[12] * right.data[0]) + (left.data[13] * right.data[4]) + (left.data[14] * right.data[8]) + (left.data[15] * right.data[12])),
			((left.data[12] * right.data[1]) + (left.data[13] * right.data[5]) + (left.data[14] * right.data[9]) + (left.data[15] * right.data[13])),
			((left.data[12] * right.data[2]) + (left.data[13] * right.data[6]) + (left.data[14] * right.data[10]) + (left.data[15] * right.data[14])),
			((left.data[12] * right.data[3]) + (left.data[13] * right.data[7]) + (left.data[14] * right.data[11])
				+ (left.data[15] * right.data[15])));
	}

	static Matrix4 Divide(const Matrix4& left, T scalar) {
		Matrix4 result(left);
		for (unsigned i = 0; i < 16; ++i) {
			result.data[i] /= scalar;
		}
		return result;
	}

	static Matrix4 Divide(const Matrix4& left, const Matrix4& right) {
		Matrix4 leftCopy(left);
		return leftCopy * Inverse(right);
	}

	static bool IsIdentity(const Matrix4& matrix) {
		return Identity.data == matrix.data;
	}

	static float GetMinor(
		float minor0, float minor1, float minor2, float minor3, float minor4, float minor5, float minor6, float minor7, float minor8) {
		return minor0 * (minor4 * minor8 - minor5 * minor7) - minor1 * (minor3 * minor8 - minor5 * minor6)
			+ minor2 * (minor3 * minor7 - minor4 * minor6);
	}

	static float Determinant(const Matrix4& matrix) {
		return matrix.data[0]
			* GetMinor(matrix.data[5], matrix.data[9], matrix.data[13], matrix.data[6], matrix.data[10], matrix.data[14], matrix.data[7],
				matrix.data[11], matrix.data[15])
			- matrix.data[4]
			* GetMinor(matrix.data[1], matrix.data[9], matrix.data[13], matrix.data[2], matrix.data[10], matrix.data[14], matrix.data[3],
				matrix.data[11], matrix.data[15])
			+ matrix.data[8]
			* GetMinor(matrix.data[1], matrix.data[5], matrix.data[13], matrix.data[2], matrix.data[6], matrix.data[14], matrix.data[3],
				matrix.data[7], matrix.data[15])
			- matrix.data[12]
			* GetMinor(matrix.data[1], matrix.data[5], matrix.data[9], matrix.data[2], matrix.data[6], matrix.data[10], matrix.data[3],
				matrix.data[7], matrix.data[11]);
	}

	static Matrix4 Transpose(const Matrix4& matrix) {
		Matrix4 TransposedMatrix(matrix);

		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				TransposedMatrix.data[4 * j + i] = matrix.data[4 * i + j];
			}
		}
		return TransposedMatrix;
	}

	static Matrix4 Inverse(const Matrix4& matrix) {
		const float determinant = Determinant(matrix);
		if (determinant == 0) {
			throw std::logic_error("Division by 0");
		}

		const float cof0 = GetMinor(matrix.getData()[5], matrix.getData()[9], matrix.getData()[13], matrix.getData()[6], matrix.getData()[10],
			matrix.getData()[14], matrix.getData()[7], matrix.getData()[11], matrix.getData()[15]);
		const float cof1 = GetMinor(matrix.getData()[1], matrix.getData()[9], matrix.getData()[13], matrix.getData()[2], matrix.getData()[10],
			matrix.getData()[14], matrix.getData()[3], matrix.getData()[11], matrix.getData()[15]);
		const float cof2 = GetMinor(matrix.getData()[1], matrix.getData()[5], matrix.getData()[13], matrix.getData()[2], matrix.getData()[6],
			matrix.getData()[14], matrix.getData()[3], matrix.getData()[7], matrix.getData()[15]);
		const float cof3 = GetMinor(matrix.getData()[1], matrix.getData()[5], matrix.getData()[9], matrix.getData()[2], matrix.getData()[6],
			matrix.getData()[10], matrix.getData()[3], matrix.getData()[7], matrix.getData()[11]);

		const float det = matrix.getData()[0] * cof0 - matrix.getData()[4] * cof1 + matrix.getData()[8] * cof2 - matrix.getData()[12] * cof3;
		if (fabs(det) <= EPSILON) {
			return Identity;
		}
		const float cof4 = GetMinor(matrix.getData()[4], matrix.getData()[8], matrix.getData()[12], matrix.getData()[6], matrix.getData()[10],
			matrix.getData()[14], matrix.getData()[7], matrix.getData()[11], matrix.getData()[15]);
		const float cof5 = GetMinor(matrix.getData()[0], matrix.getData()[8], matrix.getData()[12], matrix.getData()[2], matrix.getData()[10],
			matrix.getData()[14], matrix.getData()[3], matrix.getData()[11], matrix.getData()[15]);
		const float cof6 = GetMinor(matrix.getData()[0], matrix.getData()[4], matrix.getData()[12], matrix.getData()[2], matrix.getData()[6],
			matrix.getData()[14], matrix.getData()[3], matrix.getData()[7], matrix.getData()[15]);
		const float cof7 = GetMinor(matrix.getData()[0], matrix.getData()[4], matrix.getData()[8], matrix.getData()[2], matrix.getData()[6],
			matrix.getData()[10], matrix.getData()[3], matrix.getData()[7], matrix.getData()[11]);

		const float cof8 = GetMinor(matrix.getData()[4], matrix.getData()[8], matrix.getData()[12], matrix.getData()[5], matrix.getData()[9],
			matrix.getData()[13], matrix.getData()[7], matrix.getData()[11], matrix.getData()[15]);
		const float cof9 = GetMinor(matrix.getData()[0], matrix.getData()[8], matrix.getData()[12], matrix.getData()[1], matrix.getData()[9],
			matrix.getData()[13], matrix.getData()[3], matrix.getData()[11], matrix.getData()[15]);
		const float cof10 = GetMinor(matrix.getData()[0], matrix.getData()[4], matrix.getData()[12], matrix.getData()[1], matrix.getData()[5],
			matrix.getData()[13], matrix.getData()[3], matrix.getData()[7], matrix.getData()[15]);
		const float cof11 = GetMinor(matrix.getData()[0], matrix.getData()[4], matrix.getData()[8], matrix.getData()[1], matrix.getData()[5],
			matrix.getData()[9], matrix.getData()[3], matrix.getData()[7], matrix.getData()[11]);

		const float cof12 = GetMinor(matrix.getData()[4], matrix.getData()[8], matrix.getData()[12], matrix.getData()[5], matrix.getData()[9],
			matrix.getData()[13], matrix.getData()[6], matrix.getData()[10], matrix.getData()[14]);
		const float cof13 = GetMinor(matrix.getData()[0], matrix.getData()[8], matrix.getData()[12], matrix.getData()[1], matrix.getData()[9],
			matrix.getData()[13], matrix.getData()[2], matrix.getData()[10], matrix.getData()[14]);
		const float cof14 = GetMinor(matrix.getData()[0], matrix.getData()[4], matrix.getData()[12], matrix.getData()[1], matrix.getData()[5],
			matrix.getData()[13], matrix.getData()[2], matrix.getData()[6], matrix.getData()[14]);
		const float cof15 = GetMinor(matrix.getData()[0], matrix.getData()[4], matrix.getData()[8], matrix.getData()[1], matrix.getData()[5],
			matrix.getData()[9], matrix.getData()[2], matrix.getData()[6], matrix.getData()[10]);

		const float detInv = 1.0f / det;
		Matrix4 inverse;

		inverse.data[0] = detInv * cof0;
		inverse.data[4] = -detInv * cof4;
		inverse.data[8] = detInv * cof8;
		inverse.data[12] = -detInv * cof12;
		inverse.data[1] = -detInv * cof1;
		inverse.data[5] = detInv * cof5;
		inverse.data[9] = -detInv * cof9;
		inverse.data[13] = detInv * cof13;
		inverse.data[2] = detInv * cof2;
		inverse.data[6] = -detInv * cof6;
		inverse.data[10] = detInv * cof10;
		inverse.data[14] = -detInv * cof14;
		inverse.data[3] = -detInv * cof3;
		inverse.data[7] = detInv * cof7;
		inverse.data[11] = -detInv * cof11;
		inverse.data[15] = detInv * cof15;

		return inverse;
	}

	static Matrix4 CreatePerspective(float fov, float aspectRatio, float zNear, float zFar) {
		const float tangent = tanf(fov / 2.0f * PI / 180.0f);
		const float height = zNear * tangent;
		const float width = height * aspectRatio;

		return CreateFrustum(-width, width, -height, height, zNear, zFar);
	}

	static Matrix4 CreateOrthographic(float left, float right, float bottom, float top, float zNear, float zFar) {
		auto ortho = Identity;

		ortho(0, 0) = 2.0f / (right - left);
		ortho(1, 1) = 2.0f / (top - bottom);
		ortho(2, 2) = -2.0f / (zFar - zNear);
		ortho(0, 3) = -(right + left) / (right - left);
		ortho(1, 3) = -(top + bottom) / (top - bottom);
		ortho(2, 3) = -(zFar + zNear) / (zFar - zNear);
		ortho(3, 3) = 1.0f;

		return ortho;
	}

	static Matrix4 CreateOrthographic(float size, float aspectRatio, float zNear, float zFar) {
		auto ortho = Identity;

		const auto right = size * aspectRatio;
		const auto left = -right;

		const auto top = size;
		const auto bottom = -top;

		ortho(0, 0) = 2.0f / (right - left);
		ortho(1, 1) = 2.0f / (top - bottom);
		ortho(2, 2) = -2.0f / (zFar - zNear);
		ortho(0, 3) = -(right + left) / (right - left);
		ortho(1, 3) = -(top + bottom) / (top - bottom);
		ortho(2, 3) = -(zFar + zNear) / (zFar - zNear);
		ortho(3, 3) = 1.0f;

		return ortho;
	}

	static Matrix4 CreateView(float eyeX, float eyeY, float eyeZ, float lookX, float lookY, float lookZ, float upX, float upY, float upZ) {
		const Vector3<T> eye(eyeX, eyeY, eyeZ);
		const Vector3<T> look(lookX, lookY, lookZ);
		const Vector3<T> up(upX, upY, upZ);

		const Vector3<T> forward(eye - look);
		Vector3<T>::Normalize(forward);

		const Vector3<T> upXForward(Vector3<T>::Cross(up, forward));
		Vector3<T>::Normalize(upXForward);

		const Vector3<T> v(Vector3<T>::Cross(forward, upXForward));

		Matrix4 View;

		View.data[0] = upXForward.x;
		View.data[1] = upXForward.y;
		View.data[2] = upXForward.z;
		View.data[3] = -Vector3<T>::Dot(eye, upXForward);

		View.data[4] = v.x;
		View.data[5] = v.y;
		View.data[6] = v.z;
		View.data[7] = -Vector3<T>::Dot(eye, v);

		View.data[8] = forward.x;
		View.data[9] = forward.y;
		View.data[10] = forward.z;
		View.data[11] = -Vector3<T>::Dot(eye, forward);

		return View;
	}

	static Matrix4 CreateView(const Vector3<T>& eye, const Vector3<T>& look, const Vector3<T>& up) {
		const Vector3 forward(Vector3<T>::Normalize(eye - look));
		Vector3<T>::Normalize(forward);

		const Vector3 upXForward(Vector3<T>::Normalize(Vector3<T>::Cross(up, forward)));
		Vector3<T>::Normalize(upXForward);

		const Vector3 v(Vector3<T>::Normalize(Vector3<T>::Cross(forward, upXForward)));

		Matrix4 View;

		View.data[0] = upXForward.x;
		View.data[1] = upXForward.y;
		View.data[2] = upXForward.z;
		View.data[3] = -Vector3<T>::Dot(eye, upXForward);

		View.data[4] = v.x;
		View.data[5] = v.y;
		View.data[6] = v.z;
		View.data[7] = -Vector3<T>::Dot(eye, v);

		View.data[8] = forward.x;
		View.data[9] = forward.y;
		View.data[10] = forward.z;
		View.data[11] = -Vector3<T>::Dot(eye, forward);

		return View;
	}

	static Matrix4 CreateFrustum(float left, float right, float bottom, float top, float zNear, float zFar) {
		const float maxView = 2.0f * zNear;
		const float width = right - left;
		const float height = top - bottom;
		const float zRange = zFar - zNear;

		Matrix4 Frustum;

		Frustum.data[0] = maxView / width;
		Frustum.data[5] = maxView / height;
		Frustum.data[2] = (right + left) / width;
		Frustum.data[6] = (top + bottom) / height;
		Frustum.data[10] = (-zFar - zNear) / zRange;
		Frustum.data[14] = -1.0f;
		Frustum.data[11] = (-maxView * zFar) / zRange;
		Frustum.data[15] = 0.0f;

		return Frustum;
	}

	template <class Context> constexpr static void serde(Context& context, Matrix4& value);
};

template <class T> Matrix4<T> operator*(const float f, const Matrix4<T>& V) {
	return V * f;
}
template <class T> Matrix4<T> operator/(const float f, const Matrix4<T>& V) {
	return V / f;
}

template <class T> struct Quaternion {
	T x = 0.0;
	T y = 0.0;
	T z = 0.0;
	T w = 1.0;

	static const Quaternion Identity;
	Quaternion() = default;
	Quaternion(float in)
		: w(in) { }
	Quaternion(float x, float y, float z, float w)
		: x(x)
		, y(y)
		, z(z)
		, w(w) { };
	Quaternion(const Quaternion& in)
		: x(in.x)
		, y(in.y)
		, z(in.z)
		, w(in.w) { }
	Quaternion(Quaternion&& in) noexcept
		: x(in.x)
		, y(in.y)
		, z(in.z)
		, w(in.w) { }

	Quaternion(const Matrix3<T>& rotationMatrix) {
		float trace = rotationMatrix.getData()[0] + rotationMatrix.getData()[4] + rotationMatrix.getData()[8];
		if (trace > 0.0f) {
			// I changed M_EPSILON to 0
			float s = 0.5f / sqrt(trace + 1.0f);
			w = 0.25f / s;
			x = (rotationMatrix.getData()[7] - rotationMatrix.getData()[5]) * s;
			y = (rotationMatrix.getData()[2] - rotationMatrix.getData()[6]) * s;
			z = (rotationMatrix.getData()[3] - rotationMatrix.getData()[1]) * s;
		}
		else {
			if (rotationMatrix.getData()[0] > rotationMatrix.getData()[4] && rotationMatrix.getData()[0] > rotationMatrix.getData()[8]) {
				float s = 2.0f * sqrt(1.0f + rotationMatrix.getData()[0] - rotationMatrix.getData()[4] - rotationMatrix.getData()[8]);
				w = (rotationMatrix.getData()[7] - rotationMatrix.getData()[5]) / s;
				x = 0.25f * s;
				y = (rotationMatrix.getData()[1] + rotationMatrix.getData()[3]) / s;
				z = (rotationMatrix.getData()[2] + rotationMatrix.getData()[6]) / s;
			}
			else if (rotationMatrix.getData()[4] > rotationMatrix.getData()[8]) {
				float s = 2.0f * sqrt(1.0f + rotationMatrix.getData()[4] - rotationMatrix.getData()[0] - rotationMatrix.getData()[8]);
				w = (rotationMatrix.getData()[2] - rotationMatrix.getData()[6]) / s;
				x = (rotationMatrix.getData()[1] + rotationMatrix.getData()[3]) / s;
				y = 0.25f * s;
				z = (rotationMatrix.getData()[5] + rotationMatrix.getData()[7]) / s;
			}
			else {
				float s = 2.0f * sqrt(1.0f + rotationMatrix.getData()[8] - rotationMatrix.getData()[0] - rotationMatrix.getData()[4]);
				w = (rotationMatrix.getData()[3] - rotationMatrix.getData()[1]) / s;
				x = (rotationMatrix.getData()[2] + rotationMatrix.getData()[6]) / s;
				y = (rotationMatrix.getData()[5] + rotationMatrix.getData()[7]) / s;
				z = 0.25f * s;
			}
		}
	}

	Quaternion(const Matrix4<T>& rotationMatrix) {
		float halfSquare;

		// Check diagonal (trace)
		const float trace = rotationMatrix.getData()[0] + rotationMatrix.getData()[5] + rotationMatrix.getData()[10];

		if (trace > 0.0f) {
			const float InvSquare = 1 / sqrt(trace + 1.f);
			w = 0.5f * (1.f / InvSquare);
			halfSquare = 0.5f * InvSquare;

			x = (rotationMatrix.getData()[6] - rotationMatrix.getData()[9]) * halfSquare;
			y = (rotationMatrix.getData()[8] - rotationMatrix.getData()[2]) * halfSquare;
			z = (rotationMatrix.getData()[1] - rotationMatrix.getData()[4]) * halfSquare;
		}
		else {
			// diagonal is negative
			int i = 0;

			if (rotationMatrix.getData()[5] > rotationMatrix.getData()[0]) {
				i = 1;
			}

			if (rotationMatrix.getData()[10] > rotationMatrix.getData()[0] || rotationMatrix.getData()[10] > rotationMatrix.getData()[5]) {
				i = 2;
			}

			static const int next[3] = { 1, 2, 0 };
			const int j = next[i];
			const int k = next[j];

			halfSquare = rotationMatrix.getData()[i * 5] - rotationMatrix.getData()[j * 5] - rotationMatrix.getData()[k * 5] + 1.0f;

			const float InvSquare = 1 / sqrt(trace + 1.f);

			float qt[4];
			qt[i] = 0.5f * (1.f / InvSquare);

			halfSquare = 0.5f * InvSquare;
			// if i is 0, j is 1 and k is 2
			if (i == 0) {
				qt[3] = (rotationMatrix.getData()[6] - rotationMatrix.getData()[9]) * halfSquare;
				qt[j] = (rotationMatrix.getData()[1] + rotationMatrix.getData()[4]) * halfSquare;
				qt[k] = (rotationMatrix.getData()[2] + rotationMatrix.getData()[8]) * halfSquare;
			}
			// if i is 1, j is 2 and k is 0
			else if (i == 1) {
				qt[3] = (rotationMatrix.getData()[8] - rotationMatrix.getData()[2]) * halfSquare;
				qt[j] = (rotationMatrix.getData()[6] + rotationMatrix.getData()[9]) * halfSquare;
				qt[k] = (rotationMatrix.getData()[4] + rotationMatrix.getData()[1]) * halfSquare;
			}
			// if i is 2, j is 0 and k is 1
			else {
				qt[3] = (rotationMatrix.getData()[1] - rotationMatrix.getData()[4]) * halfSquare;
				qt[j] = (rotationMatrix.getData()[8] + rotationMatrix.getData()[2]) * halfSquare;
				qt[k] = (rotationMatrix.getData()[9] + rotationMatrix.getData()[6]) * halfSquare;
			}
			x = qt[0];
			y = qt[1];
			z = qt[2];
			w = qt[3];
		}
	}
	Quaternion(const Vector3<T>& euler) {
		/* Degree to radians then times 0.5f = 0.0087f */
		float yaw = TO_RADIANS(euler.z) * 0.5f;
		float pitch = TO_RADIANS(euler.y) * 0.5f;
		float roll = TO_RADIANS(euler.x) * 0.5f;

		float cy = cos(yaw);
		float sy = sin(yaw);
		float cp = cos(pitch);
		float sp = sin(pitch);
		float cr = cos(roll);
		float sr = sin(roll);

		x = sr * cp * cy - cr * sp * sy;
		y = cr * sp * cy + sr * cp * sy;
		z = cr * cp * sy - sr * sp * cy;
		w = cr * cp * cy + sr * sp * sy;
	}

	T getX() {
		return x;
	}
	T getY() {
		return y;
	}
	T getZ() {
		return z;
	}
	T getW() {
		return w;
	}
	void setX(T v) {
		x = v;
	}
	void setY(T v) {
		y = v;
	}
	void setZ(T v) {
		z = v;
	}
	void setW(T v) {
		w = v;
	}

	static Quaternion Normalize(const Quaternion& target) {
		if (Length(target) < 0.0001) {
			return target;
		}
		return target / Length(target);
	}

	static Matrix3<T> ToMatrix3(const Quaternion& target) {
		if (!IsNormalized(target)) {
			throw std::logic_error("Cannot convert non-normalized quaternions to Matrix4");
		}

		float y2 = target.y * target.y;
		float wz = target.w * target.z;
		float x2 = target.x * target.x;
		float z2 = target.z * target.z;
		float xz = target.x * target.z;
		float yz = target.y * target.z;
		float xy = target.x * target.y;
		float wy = target.w * target.y;
		float wx = target.w * target.x;

		Matrix3<T> converted;
		converted.getData()[0] = 1.0f - (2 * y2) - (2 * z2);
		converted.getData()[1] = (2 * xy) - (2 * wz);
		converted.getData()[2] = (2 * xz) + (2 * wy);
		converted.getData()[3] = (2 * xy) + (2 * wz);
		converted.getData()[4] = 1.0f - (2 * x2) - (2 * z2);
		converted.getData()[5] = (2 * yz) - (2 * wx);
		converted.getData()[6] = (2 * xz) - (2 * wy);
		converted.getData()[7] = (2 * yz) + (2 * wx);
		converted.getData()[8] = 1.0f - (2 * x2) - (2 * y2);
		return converted;
	}

	static Matrix4<T> ToMatrix4(const Quaternion& target) {
		if (!IsNormalized(target)) {
			printf("ToMatrix4 non-normalized: %f, %f, %f, %f (len %f)\n", target.x, target.y, target.z, target.w, Length(target));
			throw std::logic_error("Cannot convert non-normalized quaternions to Matrix4");
		}

		float y2 = target.y * target.y;
		float wz = target.w * target.z;
		float x2 = target.x * target.x;
		float z2 = target.z * target.z;
		float xz = target.x * target.z;
		float yz = target.y * target.z;
		float xy = target.x * target.y;
		float wy = target.w * target.y;
		float wx = target.w * target.x;

		Matrix4<T> converted = Matrix4<T>::Identity;
		converted.getData()[0] = 1.0f - (2 * y2) - (2 * z2);
		converted.getData()[1] = (2 * xy) - (2 * wz);
		converted.getData()[2] = (2 * xz) + (2 * wy);
		converted.getData()[3] = 0;
		converted.getData()[4] = (2 * xy) + (2 * wz);
		converted.getData()[5] = 1.0f - (2 * x2) - (2 * z2);
		converted.getData()[6] = (2 * yz) - (2 * wx);
		converted.getData()[7] = 0;
		converted.getData()[8] = (2 * xz) - (2 * wy);
		converted.getData()[9] = (2 * yz) + (2 * wx);
		converted.getData()[10] = 1.0f - (2 * x2) - (2 * y2);
		converted.getData()[11] = 0;
		converted.getData()[12] = 0;
		converted.getData()[13] = 0;
		converted.getData()[14] = 0;
		converted.getData()[15] = 1;
		return converted;
	}

	static bool IsNormalized(const Quaternion& target) {
		return std::abs(Length(target) - 1.0f) < 0.0001f;
	}

	static Vector3<T> RotatePoint(const Vector3<T>& point, const Quaternion& quaternion) {
		Vector3 Q(quaternion.x, quaternion.y, quaternion.z);
		Vector3 _T = Vector3<T>::Cross(Q, point) * 2.0f;

		return point + (_T * quaternion.w) + Vector3<T>::Cross(Q, _T);
	}

	static Vector3<T> RotatePoint(const Vector3<T>& point, const Quaternion& quaternion, const Vector3<T>& pivot) {
		Vector3 toRotate = point - pivot;
		return RotatePoint(toRotate, quaternion);
	}

	static float Length(const Quaternion& target) {
		return std::sqrt(LengthSquare(target));
	}

	static float LengthSquare(const Quaternion& target) {
		return target.x * target.x + target.y * target.y + target.z * target.z + target.w * target.w;
	}

	static Quaternion Conjugate(const Quaternion& q) {
		return Quaternion(-q.x, -q.y, -q.z, q.w);
	}

	bool operator==(const Quaternion& otherQuat) const {
		return x == otherQuat.x && y == otherQuat.y && z == otherQuat.z && w == otherQuat.w;
	}

	bool operator!=(const Quaternion& otherQuat) const {
		return x != otherQuat.x || y != otherQuat.y || z != otherQuat.z || w != otherQuat.w;
	}

	Quaternion operator+(const Quaternion& otherQuat) const {
		return Quaternion(x + otherQuat.x, y + otherQuat.y, z + otherQuat.z, w + otherQuat.w);
	}

	Quaternion& operator+=(const Quaternion& otherQuat) {
		x += otherQuat.x;
		y += otherQuat.y;
		z += otherQuat.z;
		w += otherQuat.w;
		return *this;
	}

	Quaternion operator-(const Quaternion& otherQuat) const {
		return Quaternion(x - otherQuat.x, y - otherQuat.x, z - otherQuat.z, w - otherQuat.w);
	}

	Quaternion& operator-=(const Quaternion& otherQuat) {
		x -= otherQuat.x;
		y -= otherQuat.x;
		z -= otherQuat.z;
		w -= otherQuat.w;
		return *this;
	}

	float operator|(const Quaternion& otherQuat) const {
		return (x * otherQuat.x + y * otherQuat.x + z * otherQuat.z + w * otherQuat.w);
	}

	Quaternion operator*(float scale) const {
		Quaternion result(*this);
		result.x *= scale;
		result.y *= scale;
		result.z *= scale;
		result.w *= scale;

		return result;
	}

	Quaternion& operator*=(const float scale) {
		x *= scale;
		y *= scale;
		z *= scale;
		w *= scale;

		return *this;
	}

	Quaternion operator*(const Quaternion& otherQuat) const {
		return Quaternion(x * otherQuat.w + y * otherQuat.z - z * otherQuat.y + w * otherQuat.x,
			-x * otherQuat.z + y * otherQuat.w + z * otherQuat.x + w * otherQuat.y,
			x * otherQuat.y - y * otherQuat.x + z * otherQuat.w + w * otherQuat.z,
			-x * otherQuat.x - y * otherQuat.y - z * otherQuat.z + w * otherQuat.w);
	}

	Quaternion& operator*=(const Quaternion& otherQuat) {
		*this = *this * otherQuat;
		return *this;
	}

	Vector3<T> operator*(const Vector3<T>& toMultiply) const {
		const float num = x * 2.0f;
		const float num2 = y * 2.0f;
		const float num3 = z * 2.0f;
		const float num4 = x * num;
		const float num5 = y * num2;
		const float num6 = z * num3;
		const float num7 = x * num2;
		const float num8 = x * num3;
		const float num9 = y * num3;
		const float num10 = w * num;
		const float num11 = w * num2;
		const float num12 = w * num3;
		Vector3<T> result;
		result.x = (1.f - (num5 + num6)) * toMultiply.x + (num7 - num12) * toMultiply.y + (num8 + num11) * toMultiply.z;
		result.y = (num7 + num12) * toMultiply.x + (1.f - (num4 + num6)) * toMultiply.y + (num9 - num10) * toMultiply.z;
		result.z = (num8 - num11) * toMultiply.x + (num9 + num10) * toMultiply.y + (1.f - (num4 + num5)) * toMultiply.z;
		return result;
	}

	Matrix3<T> operator*(const Matrix3<T>& multiply) const {
		return (ToMatrix3(*this) * multiply);
	}

	Quaternion& operator/=(const float scale) {
		const float reciprocate = 1.0f / scale;
		x *= reciprocate;
		y *= reciprocate;
		z *= reciprocate;
		w *= reciprocate;

		return *this;
	}

	Quaternion operator/(const float scale) const {
		Quaternion temp(*this);
		const float reciprocate = 1.0f / scale;
		temp.x *= reciprocate;
		temp.y *= reciprocate;
		temp.z *= reciprocate;
		temp.w *= reciprocate;

		return temp;
	}

	static Quaternion LookAt(const Vector3<T>& forward, const Vector3<T>& up) {
		auto vector = Vector3<T>::Normalize(forward);
		auto vector2 = Vector3<T>::Normalize(Vector3<T>::Cross(up, vector));
		auto vector3 = Vector3<T>::Cross(vector, vector2);
		auto m00 = vector2.x;
		auto m01 = vector2.y;
		auto m02 = vector2.z;
		auto m10 = vector3.x;
		auto m11 = vector3.y;
		auto m12 = vector3.z;
		auto m20 = vector.x;
		auto m21 = vector.y;
		auto m22 = vector.z;

		float num8 = (m00 + m11) + m22;
		auto quaternion = Identity;
		if (num8 > 0.f) {
			auto num = sqrt(num8 + 1.f);
			quaternion.w = num * 0.5f;
			num = 0.5f / num;
			quaternion.x = (m12 - m21) * num;
			quaternion.y = (m20 - m02) * num;
			quaternion.z = (m01 - m10) * num;
			return quaternion;
		}
		if ((m00 >= m11) && (m00 >= m22)) {
			auto num7 = sqrt(((1.f + m00) - m11) - m22);
			auto num4 = 0.5f / num7;
			quaternion.x = 0.5f * num7;
			quaternion.y = (m01 + m10) * num4;
			quaternion.z = (m02 + m20) * num4;
			quaternion.w = (m12 - m21) * num4;
			return quaternion;
		}
		if (m11 > m22) {
			auto num6 = sqrt(((1.f + m11) - m00) - m22);
			auto num3 = 0.5f / num6;
			quaternion.x = (m10 + m01) * num3;
			quaternion.y = 0.5f * num6;
			quaternion.z = (m21 + m12) * num3;
			quaternion.w = (m20 - m02) * num3;
			return quaternion;
		}
		auto num5 = sqrt(((1.f + m22) - m00) - m11);
		auto num2 = 0.5f / num5;
		quaternion.x = (m20 + m02) * num2;
		quaternion.y = (m21 + m12) * num2;
		quaternion.z = 0.5f * num5;
		quaternion.w = (m01 - m10) * num2;
		return quaternion;
	}

	void normalise() {
		*this = Normalize(*this);
	}

	void addScaledVector(const Vector3<T>& vector, float scale) {
		Quaternion q(vector.x * scale, vector.y * scale, vector.z * scale, 0);
		q *= *this;
		w += q.w * 0.5f;
		x += q.x * 0.5f;
		y += q.y * 0.5f;
		z += q.z * 0.5f;
	}

	static float Dot(const Quaternion& V1, const Quaternion& V2) {
		return V1.x * V2.x + V1.y * V2.y + V1.z * V2.z + V1.w * V2.w;
	}

	static Quaternion Slerp(const Quaternion& x, const Quaternion& y, float a) {
		Quaternion z = y;

		float cosTheta = Dot(x, y);

		// If cosTheta < 0, the interpolation will take the long way around the sphere.
		// To fix this, one quat must be negated.
		if (cosTheta < 0.0f) {
			z = y * -1.0f;
			cosTheta = -cosTheta;
		}
		auto mix = [](float x, float y, float a) { return x * (1.0f - a) + y * a; };
		// Perform a linear interpolation when cosTheta is close to 1 to avoid side effect of sin(angle) becoming a zero denominator
		if (cosTheta > 1.0f - std::numeric_limits<float>::epsilon()) {
			// Linear interpolation
			return Quaternion(mix(x.x, z.x, a), mix(x.y, z.y, a), mix(x.z, z.z, a), mix(x.w, z.w, a));
		}
		else {
			// Essential Mathematics, page 467
			auto angle = acos(cosTheta);

			auto sin1 = sin((1.0f - a) * angle);
			auto sin2 = sin(a * angle);
			auto s = sin(angle);

			auto xsin1 = x * sin1;
			auto zsin2 = z * sin2;

			auto res = xsin1 + zsin2;
			auto res2 = res / s;

			return ((x * sin((1.0f - a) * angle)) + (z * sin(a * angle))) / sin(angle);
		}
	}

	static Vector3<T> ToEulerAngles(Quaternion q) {
		Vector3<T> angles;

		// roll (x-axis rotation)
		float sinr_cosp = 2 * (q.w * q.x + q.y * q.z);
		float cosr_cosp = 1 - 2 * (q.x * q.x + q.y * q.y);
		angles.x = std::atan2(sinr_cosp, cosr_cosp);

		// pitch (y-axis rotation)
		float sinp = std::sqrt(1 + 2 * (q.w * q.y - q.x * q.z));
		float cosp = std::sqrt(1 - 2 * (q.w * q.y - q.x * q.z));
		angles.y = 2 * std::atan2(sinp, cosp) - PI / 2;

		// yaw (z-axis rotation)
		float siny_cosp = 2 * (q.w * q.z + q.x * q.y);
		float cosy_cosp = 1 - 2 * (q.y * q.y + q.z * q.z);
		angles.z = std::atan2(siny_cosp, cosy_cosp);

		return angles;
	}

	static Quaternion ToQuaternion(Vector3<T> vec) {
		auto yaw = vec.x;
		auto pitch = vec.y;
		auto roll = vec.z;
		float cr = cos(roll * 0.5);
		float sr = sin(roll * 0.5);
		float cp = cos(pitch * 0.5);
		float sp = sin(pitch * 0.5);
		float cy = cos(yaw * 0.5);
		float sy = sin(yaw * 0.5);

		Quaternion q;
		q.w = cr * cp * cy + sr * sp * sy;
		q.x = sr * cp * cy - cr * sp * sy;
		q.y = cr * sp * cy + sr * cp * sy;
		q.z = cr * cp * sy - sr * sp * cy;

		return q;
	}

	Quaternion<T>& operator=(const Quaternion<T>& v) {
		if (this == &v) {
			return *this;
		}
		x = v.x;
		y = v.y;
		z = v.z;
		w = v.w;
		return *this;
	}
};

template <class T> Quaternion<T> operator*(const float f, const Quaternion<T>& V) {
	return V * f;
}

template <class T> Quaternion<T> operator/(const float f, const Quaternion<T>& V) {
	return V / f;
}

using Vector3i = Vector3<int>;
using Vector3u = Vector3<unsigned>;
using Vector3f = Vector3<float>;
using Vector3d = Vector3<double>;

using Vector4i = Vector4<int>;
using Vector4u = Vector4<unsigned>;
using Vector4f = Vector4<float>;
using Vector4d = Vector4<double>;

using Matrix3i = Matrix3<int>;
using Matrix3u = Matrix3<unsigned>;
using Matrix3f = Matrix3<float>;
using Matrix3d = Matrix3<double>;

using Matrix4i = Matrix4<int>;
using Matrix4u = Matrix4<unsigned>;
using Matrix4f = Matrix4<float>;
using Matrix4d = Matrix4<double>;

using QuaternionI = Quaternion<int>;
using QuaternionU = Quaternion<unsigned>;
using QuaternionF = Quaternion<float>;
using QuaternionD = Quaternion<double>;

template <typename T> const Vector3<T> Vector3<T>::One = Vector3<T>(1.0f, 1.0f, 1.0f);
template <typename T> const Vector3<T> Vector3<T>::Zero = Vector3<T>(0.0f, 0.0f, 0.0f);
template <typename T> const Vector3<T> Vector3<T>::Forward = Vector3<T>(0.0f, 0.0f, 1.0f);
template <typename T> const Vector3<T> Vector3<T>::Right = Vector3<T>(1.0f, 0.0f, 0.0f);
template <typename T> const Vector3<T> Vector3<T>::Up = Vector3<T>(0.0f, 1.0f, 0.0f);

template <typename T> const Vector4<T> Vector4<T>::One = Vector4<T>(1.0f, 1.0f, 1.0f, 1.0f);
template <typename T> const Vector4<T> Vector4<T>::Zero = Vector4<T>(0.0f, 0.0f, 0.0f, 0.0f);

template <typename T> const Matrix3<T> Matrix3<T>::Identity = Matrix3<T>(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
template <typename T> const Matrix3<T> Matrix3<T>::Zero = Matrix3<T>(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

template <typename T>
const Matrix4<T> Matrix4<T>::Identity = Matrix4<T>(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0);

template <typename T> const Matrix4<T> Matrix4<T>::Zero = Matrix4<T>(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

template <typename T> const Quaternion<T> Quaternion<T>::Identity = Quaternion<T>(0.0f, 0.0f, 0.0f, 1.0f);
}
