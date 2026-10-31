#pragma once
#include <cstddef>
#include <cstdint>

#include "renderEnums.h"

namespace IKIGAI::RENDER {
// CPU decoding for half-float and packed float texels returned by readTexture.
bool IsPackedFloatFormat(PixelFormat format);
// Bytes per texel as stored on the GPU. Only valid for packed float formats.
size_t PackedFloatTexelBytes(PixelFormat format);
// Floats per texel in readTexture output: RG16_FLOAT gives 2, the others give 4.
size_t PackedFloatReadbackChannels(PixelFormat format);
float HalfToFloat(uint16_t value);
// Decodes R11G11B10_FLOAT (DXGI) / B10G11R11_UFLOAT_PACK32 (Vulkan): R in the low bits.
void UnpackR11G11B10(uint32_t value, float& red, float& green, float& blue);
// Decodes texelCount texels from source into destination with readTexture's channel layout.
// Single-channel R16_FLOAT expands to (r, 0, 0, 1) like R_FLOAT readback.
void DecodePackedFloatTexels(PixelFormat format, const uint8_t* source, size_t texelCount, float* destination);
}
