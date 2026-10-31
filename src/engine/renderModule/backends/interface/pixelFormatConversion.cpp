#include "pixelFormatConversion.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace IKIGAI::RENDER {
namespace {
	// Unsigned small float with a 5-bit exponent (bias 15), as used by R11G11B10.
	float UnsignedSmallFloatToFloat(uint32_t exponent, uint32_t mantissa, uint32_t mantissaBits) {
		const float scale = static_cast<float>(1u << mantissaBits);
		if (exponent == 0) {
			return std::ldexp(static_cast<float>(mantissa) / scale, -14);
		}
		if (exponent == 31) {
			return mantissa == 0 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
		}
		return std::ldexp(1.0f + static_cast<float>(mantissa) / scale, static_cast<int>(exponent) - 15);
	}

	uint16_t LoadUint16(const uint8_t* source) {
		uint16_t value = 0;
		std::memcpy(&value, source, sizeof(value));
		return value;
	}

	uint32_t LoadUint32(const uint8_t* source) {
		uint32_t value = 0;
		std::memcpy(&value, source, sizeof(value));
		return value;
	}
}

bool IsPackedFloatFormat(PixelFormat format) {
	switch (format) {
	case PixelFormat::RGBA16_FLOAT:
	case PixelFormat::RG16_FLOAT:
	case PixelFormat::R16_FLOAT:
	case PixelFormat::R11G11B10_FLOAT:
		return true;
	default:
		return false;
	}
}

size_t PackedFloatTexelBytes(PixelFormat format) {
	switch (format) {
	case PixelFormat::RGBA16_FLOAT:
		return 8;
	case PixelFormat::RG16_FLOAT:
		return 4;
	case PixelFormat::R16_FLOAT:
		return 2;
	case PixelFormat::R11G11B10_FLOAT:
		return 4;
	default:
		throw std::invalid_argument("PackedFloatTexelBytes: not a packed float format");
	}
}

size_t PackedFloatReadbackChannels(PixelFormat format) {
	switch (format) {
	case PixelFormat::RG16_FLOAT:
		return 2;
	case PixelFormat::RGBA16_FLOAT:
	case PixelFormat::R16_FLOAT:
	case PixelFormat::R11G11B10_FLOAT:
		return 4;
	default:
		throw std::invalid_argument("PackedFloatReadbackChannels: not a packed float format");
	}
}

float HalfToFloat(uint16_t value) {
	const float sign = (value & 0x8000u) != 0 ? -1.0f : 1.0f;
	const uint32_t exponent = (value >> 10) & 0x1Fu;
	const uint32_t mantissa = value & 0x3FFu;
	if (exponent == 0) {
		return sign * std::ldexp(static_cast<float>(mantissa) / 1024.0f, -14);
	}
	if (exponent == 31) {
		return mantissa == 0 ? sign * std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
	}
	return sign * std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f, static_cast<int>(exponent) - 15);
}

void UnpackR11G11B10(uint32_t value, float& red, float& green, float& blue) {
	const uint32_t redBits = value & 0x7FFu;
	const uint32_t greenBits = (value >> 11) & 0x7FFu;
	const uint32_t blueBits = (value >> 22) & 0x3FFu;
	red = UnsignedSmallFloatToFloat(redBits >> 6, redBits & 0x3Fu, 6);
	green = UnsignedSmallFloatToFloat(greenBits >> 6, greenBits & 0x3Fu, 6);
	blue = UnsignedSmallFloatToFloat(blueBits >> 5, blueBits & 0x1Fu, 5);
}

void DecodePackedFloatTexels(PixelFormat format, const uint8_t* source, size_t texelCount, float* destination) {
	switch (format) {
	case PixelFormat::RGBA16_FLOAT:
		for (size_t texel = 0; texel < texelCount; ++texel) {
			for (size_t channel = 0; channel < 4; ++channel) {
				destination[texel * 4 + channel] = HalfToFloat(LoadUint16(source + texel * 8 + channel * 2));
			}
		}
		return;
	case PixelFormat::RG16_FLOAT:
		for (size_t texel = 0; texel < texelCount; ++texel) {
			destination[texel * 2] = HalfToFloat(LoadUint16(source + texel * 4));
			destination[texel * 2 + 1] = HalfToFloat(LoadUint16(source + texel * 4 + 2));
		}
		return;
	case PixelFormat::R16_FLOAT:
		for (size_t texel = 0; texel < texelCount; ++texel) {
			destination[texel * 4] = HalfToFloat(LoadUint16(source + texel * 2));
			destination[texel * 4 + 1] = 0.0f;
			destination[texel * 4 + 2] = 0.0f;
			destination[texel * 4 + 3] = 1.0f;
		}
		return;
	case PixelFormat::R11G11B10_FLOAT:
		for (size_t texel = 0; texel < texelCount; ++texel) {
			UnpackR11G11B10(LoadUint32(source + texel * 4), destination[texel * 4], destination[texel * 4 + 1], destination[texel * 4 + 2]);
			destination[texel * 4 + 3] = 1.0f;
		}
		return;
	default:
		throw std::invalid_argument("DecodePackedFloatTexels: not a packed float format");
	}
}
}
