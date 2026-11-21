#include "simplexNoise.h"

#include <algorithm>
#include <cmath>

// Adapted from GLM's simplex noise, based on Stefan Gustavson and Ashima Arts
// webgl-noise.
/*
Copyright (c) 2005 - G-Truc Creation

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
namespace IKIGAI::MATH {
namespace {
	float mod289(float value) {
		return value - std::floor(value * (1.f / 289.f)) * 289.f;
	}

	float permute(float value) {
		return mod289((34.f * value + 1.f) * value);
	}

	float inverseGradientLength(float lengthSquared) {
		return 1.79284291400159f - 0.85373472095314f * lengthSquared;
	}

	float corner2(Vector2f offset, float permutation) {
		const float scaled = permutation * 0.024390243902439f;
		const float x = 2.f * (scaled - std::floor(scaled)) - 1.f;
		const float height = std::abs(x) - 0.5f;
		const float gradientX = x - std::floor(x + 0.5f);
		const Vector2f gradient(gradientX, height);
		float weight = std::max(0.5f - (offset.x * offset.x + offset.y * offset.y), 0.f);
		weight *= weight;
		weight *= weight;
		weight *= inverseGradientLength(gradientX * gradientX + height * height);
		return weight * (gradient.x * offset.x + gradient.y * offset.y);
	}

	float corner3(Vector3f offset, float permutation) {
		constexpr float oneSeventh = 0.142857142857f;
		const float index = permutation - 49.f * std::floor(permutation * oneSeventh * oneSeventh);
		const float ix = std::floor(index * oneSeventh);
		const float iy = std::floor(index - 7.f * ix);
		const float x = ix * (2.f * oneSeventh) + (0.5f * oneSeventh - 1.f);
		const float y = iy * (2.f * oneSeventh) + (0.5f * oneSeventh - 1.f);
		const float height = 1.f - std::abs(x) - std::abs(y);
		const float shift = height <= 0.f ? -1.f : 0.f;
		auto gradient = Vector3f(x + (2.f * std::floor(x) + 1.f) * shift, y + (2.f * std::floor(y) + 1.f) * shift, height);
		gradient *= inverseGradientLength(Vector3f::LengthSquare(gradient));
		float weight = std::max(0.6f - Vector3f::LengthSquare(offset), 0.f);
		weight *= weight;
		return (weight * weight) * Vector3f::Dot(gradient, offset);
	}
}

float SimplexNoise(Vector2f point) {
	constexpr float skew = 0.366025403784439f;
	constexpr float unskew = 0.211324865405187f;
	constexpr float lastOffset = -0.577350269189626f;
	const float skewed = point.x * skew + point.y * skew;
	Vector2f cell(std::floor(point.x + skewed), std::floor(point.y + skewed));
	const float unskewed = cell.x * unskew + cell.y * unskew;
	const Vector2f origin = point - cell + Vector2f(unskewed, unskewed);
	const Vector2f middle = origin.x > origin.y ? Vector2f(1.f, 0.f) : Vector2f(0.f, 1.f);
	cell.x -= 289.f * std::floor(cell.x / 289.f);
	cell.y -= 289.f * std::floor(cell.y / 289.f);
	const auto sample = [&](Vector2f offset, Vector2f corner) { return corner2(offset, permute(permute(cell.y + corner.y) + cell.x + corner.x)); };
	return 130.f
		* (sample(origin, {}) + sample(origin + Vector2f(unskew, unskew) - middle, middle)
			+ sample(origin + Vector2f(lastOffset, lastOffset), { 1.f, 1.f }));
}

float SimplexNoise(Vector3f point) {
	constexpr float skew = 1.f / 3.f;
	constexpr float unskew = 1.f / 6.f;
	const float skewed = Vector3f::Dot(point, Vector3f(skew));
	Vector3f cell(std::floor(point.x + skewed), std::floor(point.y + skewed), std::floor(point.z + skewed));
	const Vector3f origin = point - cell + Vector3f(Vector3f::Dot(cell, Vector3f(unskew)));
	const Vector3f rank(origin.x >= origin.y ? 1.f : 0.f, origin.y >= origin.z ? 1.f : 0.f, origin.z >= origin.x ? 1.f : 0.f);
	const Vector3f opposite(1.f - rank.z, 1.f - rank.x, 1.f - rank.y);
	const Vector3f first(std::min(rank.x, opposite.x), std::min(rank.y, opposite.y), std::min(rank.z, opposite.z));
	const Vector3f second(std::max(rank.x, opposite.x), std::max(rank.y, opposite.y), std::max(rank.z, opposite.z));
	cell = { mod289(cell.x), mod289(cell.y), mod289(cell.z) };
	const auto sample = [&](Vector3f offset, Vector3f corner) {
		return corner3(offset, permute(permute(permute(cell.z + corner.z) + cell.y + corner.y) + cell.x + corner.x));
	};
	return 42.f
		* (sample(origin, {}) + sample(origin - first + Vector3f(unskew), first) + sample(origin - second + Vector3f(skew), second)
			+ sample(origin - Vector3f(0.5f), { 1.f, 1.f, 1.f }));
}
}
