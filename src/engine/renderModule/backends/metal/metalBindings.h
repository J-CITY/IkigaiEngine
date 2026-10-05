#pragma once
#include <cstdint>

namespace IKIGAI::RENDER {
	// Buffer 0 is the vertex-fetch buffer. SPIR-V descriptor bindings are shifted
	// so they do not alias it. Indices from 20 upward are reserved for
	// tessellation kernels (same slots SPIRV-Cross uses) and push constants.
	inline constexpr uint32_t kMetalVertexBufferIndex = 0;
	inline constexpr uint32_t kMetalResourceBufferBase = 1;
	inline constexpr uint32_t kMetalUserBufferLimit = 20;
	inline constexpr uint32_t kMetalShaderPatchInputBufferIndex = 20;
	inline constexpr uint32_t kMetalShaderIndexBufferIndex = 21;
	inline constexpr uint32_t kMetalShaderInputBufferIndex = 22;
	inline constexpr uint32_t kMetalTessFactorBufferIndex = 26;
	inline constexpr uint32_t kMetalPatchOutputBufferIndex = 27;
	inline constexpr uint32_t kMetalShaderOutputBufferIndex = 28;
	inline constexpr uint32_t kMetalIndirectParamsBufferIndex = 29;
	inline constexpr uint32_t kMetalPushConstantBufferIndex = 30;

	inline uint32_t MetalBufferIndexForBinding(uint32_t binding) {
		const uint32_t index = binding + kMetalResourceBufferBase;
		if (index >= kMetalUserBufferLimit) {
			return kMetalUserBufferLimit - 1;
		}
		return index;
	}
}
