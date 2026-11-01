#include "vmeshEncoding.h"

#include <algorithm>
#include <cmath>

namespace IKIGAI::VG {
namespace {
	float SignNotZero(float value) {
		return value >= 0.0f ? 1.0f : -1.0f;
	}

	VMeshFloat3 Normalized(const VMeshFloat3& value) {
		const double length = std::sqrt(double(value.X) * value.X + double(value.Y) * value.Y + double(value.Z) * value.Z);
		if (!(length > 0.0) || !std::isfinite(length)) {
			return VMeshFloat3 { 0.0f, 0.0f, 1.0f };
		}
		return VMeshFloat3 { float(value.X / length), float(value.Y / length), float(value.Z / length) };
	}

	int32_t Quantize(float value, float scale) {
		const float clamped = std::clamp(value, -1.0f, 1.0f);
		return int32_t(std::lround(clamped * scale));
	}

	int32_t SignExtend15(uint32_t bits) {
		int32_t value = int32_t(bits & VMeshTangentComponentMask);
		if (value & 0x4000) {
			value -= 0x8000;
		}
		return value;
	}
}

void OctahedralEncode(const VMeshFloat3& unitVector, float& outX, float& outY) {
	const VMeshFloat3 n = Normalized(unitVector);
	const float sum = std::abs(n.X) + std::abs(n.Y) + std::abs(n.Z);
	float x = n.X / sum;
	float y = n.Y / sum;
	if (n.Z < 0.0f) {
		const float wrappedX = (1.0f - std::abs(y)) * SignNotZero(x);
		const float wrappedY = (1.0f - std::abs(x)) * SignNotZero(y);
		x = wrappedX;
		y = wrappedY;
	}
	outX = x;
	outY = y;
}

VMeshFloat3 OctahedralDecode(float x, float y) {
	VMeshFloat3 v { x, y, 1.0f - std::abs(x) - std::abs(y) };
	if (v.Z < 0.0f) {
		const float unwrappedX = (1.0f - std::abs(y)) * SignNotZero(x);
		const float unwrappedY = (1.0f - std::abs(x)) * SignNotZero(y);
		v.X = unwrappedX;
		v.Y = unwrappedY;
	}
	return Normalized(v);
}

uint32_t EncodeNormal(const VMeshFloat3& normal) {
	float x = 0.0f;
	float y = 0.0f;
	OctahedralEncode(normal, x, y);
	const uint32_t qx = uint32_t(uint16_t(int16_t(Quantize(x, VMeshNormalScale))));
	const uint32_t qy = uint32_t(uint16_t(int16_t(Quantize(y, VMeshNormalScale))));
	return qx | (qy << 16);
}

VMeshFloat3 DecodeNormal(uint32_t encoded) {
	const float x = std::max(float(int16_t(uint16_t(encoded & 0xFFFFu))) / VMeshNormalScale, -1.0f);
	const float y = std::max(float(int16_t(uint16_t(encoded >> 16))) / VMeshNormalScale, -1.0f);
	return OctahedralDecode(x, y);
}

uint32_t EncodeTangent(const VMeshFloat3& tangent, float bitangentSign) {
	float x = 0.0f;
	float y = 0.0f;
	OctahedralEncode(tangent, x, y);
	const uint32_t qx = uint32_t(Quantize(x, VMeshTangentScale)) & VMeshTangentComponentMask;
	const uint32_t qy = uint32_t(Quantize(y, VMeshTangentScale)) & VMeshTangentComponentMask;
	uint32_t encoded = qx | (qy << 15);
	if (bitangentSign < 0.0f) {
		encoded |= VMeshTangentSignBit;
	}
	return encoded;
}

uint32_t EncodeTangent(const VMeshFloat3& normal, const VMeshFloat3& tangent, const VMeshFloat3& bitangent) {
	const VMeshFloat3 crossNT { normal.Y * tangent.Z - normal.Z * tangent.Y, normal.Z * tangent.X - normal.X * tangent.Z,
		normal.X * tangent.Y - normal.Y * tangent.X };
	const float dot = crossNT.X * bitangent.X + crossNT.Y * bitangent.Y + crossNT.Z * bitangent.Z;
	return EncodeTangent(tangent, dot < 0.0f ? -1.0f : 1.0f);
}

VMeshDecodedTangent DecodeTangent(uint32_t encoded) {
	const float x = float(SignExtend15(encoded)) / VMeshTangentScale;
	const float y = float(SignExtend15(encoded >> 15)) / VMeshTangentScale;
	VMeshDecodedTangent decoded;
	decoded.Tangent = OctahedralDecode(std::clamp(x, -1.0f, 1.0f), std::clamp(y, -1.0f, 1.0f));
	decoded.BitangentSign = (encoded & VMeshTangentSignBit) ? -1.0f : 1.0f;
	return decoded;
}

float AngleBetweenDegrees(const VMeshFloat3& a, const VMeshFloat3& b) {
	const double lengthA = std::sqrt(double(a.X) * a.X + double(a.Y) * a.Y + double(a.Z) * a.Z);
	const double lengthB = std::sqrt(double(b.X) * b.X + double(b.Y) * b.Y + double(b.Z) * b.Z);
	if (!(lengthA > 0.0) || !(lengthB > 0.0)) {
		return 0.0f;
	}
	// atan2 of |a x b| and a.b is accurate for small angles, unlike acos of the dot product.
	const double crossX = double(a.Y) * b.Z - double(a.Z) * b.Y;
	const double crossY = double(a.Z) * b.X - double(a.X) * b.Z;
	const double crossZ = double(a.X) * b.Y - double(a.Y) * b.X;
	const double crossLength = std::sqrt(crossX * crossX + crossY * crossY + crossZ * crossZ);
	const double dot = double(a.X) * b.X + double(a.Y) * b.Y + double(a.Z) * b.Z;
	return float(std::atan2(crossLength, dot) * 180.0 / 3.14159265358979323846);
}
}
