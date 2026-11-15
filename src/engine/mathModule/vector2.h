#pragma once
// Lightweight canonical Vector2 math. Include math.h when serde is needed.
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace IKIGAI::MATH {
constexpr int math_max(int a) {
	return a;
}
template <typename... Args> constexpr int math_max(int a, Args... args) {
	int b = math_max(args...);
	return a > b ? a : b;
}

// A swizzle view owns pointers to live scalar members, never an overlapping union.
template <template <typename> class TT, typename T, int... Indexes> class Swizzle {
public:
	explicit Swizzle(std::array<T*, sizeof...(Indexes)> values)
		: mValues(values) { }
	Swizzle& operator=(const TT<T>& rhs) {
		const TT<T> copy(rhs);
		for (size_t i = 0; i < mValues.size(); ++i) {
			*mValues[i] = copy[static_cast<int>(i)];
		}
		return *this;
	}
	Swizzle& operator=(const Swizzle& rhs) {
		return *this = static_cast<TT<T>>(rhs);
	}
	operator TT<T>() const {
		return read(std::make_index_sequence<sizeof...(Indexes)> {});
	}

private:
	template <size_t... I> TT<T> read(std::index_sequence<I...>) const {
		return TT<T>(*mValues[I]...);
	}
	std::array<T*, sizeof...(Indexes)> mValues;
};

template <typename T> struct Vector2 {
	T x {};
	T y {};
	T& width() {
		return x;
	}
	const T& width() const {
		return x;
	}
	T& heights() {
		return y;
	}
	const T& heights() const {
		return y;
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
#undef IKIGAI_VECTOR_SWIZZLE
	std::array<T, 2> toArray() const {
		return { x, y };
	}
	void fromArray(const std::array<T, 2>& values) {
		x = values[0];
		y = values[1];
	}

	Vector2()
		: x(0)
		, y(0) { };
	Vector2(T _v)
		: x(_v)
		, y(_v) { };
	Vector2(T _x, T _y)
		: x(_x)
		, y(_y) { };

	T getX() {
		return x;
	}
	T getY() {
		return y;
	}
	void setX(T v) {
		x = v;
	}
	void setY(T v) {
		y = v;
	}

	T& operator[](const int index) {
		switch (index) {
		case 0:
			return x;
		case 1:
			return y;
		default:
			break;
		}
		throw std::out_of_range("Vector2: out of range");
	}
	const T& operator[](const int index) const {
		switch (index) {
		case 0:
			return x;
		case 1:
			return y;
		default:
			break;
		}
		throw std::out_of_range("Vector2: out of range");
	}

	Vector2& operator=(const Vector2& right) {
		if (this == &right) {
			return *this;
		}
		x = right.x;
		y = right.y;
		return *this;
	}

	inline bool operator==(const Vector2& V2) const {
		return this->x == V2.x && this->y == V2.y;
	}

	inline bool operator!=(const Vector2& V2) const {
		return !operator==(V2);
	}

	inline Vector2 operator-() const {
		return Vector2(-this->x, -this->y);
	}

	inline void operator+=(const Vector2& V2) {
		for (int i = 0; i < 2; ++i) {
			(*this)[i] += V2[i];
		}
	}

	inline void operator-=(const Vector2& V2) {
		for (int i = 0; i < 2; ++i) {
			(*this)[i] -= V2[i];
		}
	}

	inline Vector2 operator+(const Vector2& V2) const {
		return Vector2(this->x + V2.x, this->y + V2.y);
	}

	inline Vector2 operator-(const Vector2& V2) const {
		return Vector2(this->x - V2.x, this->y - V2.y);
	}

	inline Vector2 operator*(const Vector2& V2) const {
		return Vector2(this->x * V2.x, this->y * V2.y);
	}

	inline Vector2 operator*(float f) const {
		return Vector2(this->x * f, this->y * f);
	}

	inline Vector2& operator*=(float scalar) {
		x *= scalar;
		y *= scalar;
		return *this;
	}

	inline Vector2 operator/(float f) const {
		return Vector2(this->x / f, this->y / f);
	}

	inline float dot(const Vector2& V2) const {
		return this->x * V2.x + this->y * V2.y;
	}

	inline float length() const {
		return std::sqrt(std::pow(x, 2) + std::pow(y, 2));
	}

	inline Vector2 normalize() const {
		return *this / length();
	}

	void clear() {
		x = y = 0;
	}

	// Serialization is defined by math.h; this header has no serde dependency.
	template <class Context> constexpr static auto serde(Context& context, Vector2& value);
};

template <class T> Vector2<T> operator*(const float f, const Vector2<T>& V) {
	return Vector2<T>(V.x * f, V.y * f);
}

using Vector2i = Vector2<int>;
using Vector2u = Vector2<unsigned>;
using Vector2f = Vector2<float>;
using Vector2d = Vector2<double>;

}
