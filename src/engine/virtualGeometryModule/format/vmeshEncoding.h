#pragma once

#include <cstdint>

// Vertex attribute encoding of the .vmesh format (VMeshVertex::Normal and VMeshVertex::Tangent).
// The formulas are the contract shared with the GLSL decoder; see docs/rendering/m2-virtual-geometry-spec.md,
// section "Vertex encoding". Available on every platform (no meshoptimizer dependency).
namespace IKIGAI::VG {
struct VMeshFloat3 {
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;
};

struct VMeshDecodedTangent {
	VMeshFloat3 Tangent {};
	float BitangentSign = 1.0f; // bitangent = BitangentSign * cross(normal, tangent)
};

inline constexpr float VMeshNormalScale = 32767.0f;
inline constexpr float VMeshTangentScale = 16383.0f;
inline constexpr uint32_t VMeshTangentComponentMask = 0x7FFFu;
inline constexpr uint32_t VMeshTangentSignBit = 1u << 31;
inline constexpr uint32_t VMeshTangentReservedBit = 1u << 30;

// Octahedral projection of a unit vector into [-1, 1]^2 (no quantization). A zero or non-finite input maps to +Z.
void OctahedralEncode(const VMeshFloat3& unitVector, float& outX, float& outY);
// Inverse of OctahedralEncode, returns a normalized vector.
VMeshFloat3 OctahedralDecode(float x, float y);

// Normal: two snorm16, x in bits 0..15, y in bits 16..31. The input is normalized internally.
uint32_t EncodeNormal(const VMeshFloat3& normal);
VMeshFloat3 DecodeNormal(uint32_t encoded);

// Tangent: two 15-bit two's complement components scaled by 16383 (x in bits 0..14, y in bits 15..29),
// bit 31 set when the bitangent sign is negative, bit 30 reserved (0). The tangent is normalized internally.
uint32_t EncodeTangent(const VMeshFloat3& tangent, float bitangentSign);
// Computes the bitangent sign as dot(cross(normal, tangent), bitangent) < 0 ? -1 : 1.
uint32_t EncodeTangent(const VMeshFloat3& normal, const VMeshFloat3& tangent, const VMeshFloat3& bitangent);
VMeshDecodedTangent DecodeTangent(uint32_t encoded);

// Angle in degrees between two vectors (0 when either is zero). Used by tests and validation tools.
float AngleBetweenDegrees(const VMeshFloat3& a, const VMeshFloat3& b);
}
