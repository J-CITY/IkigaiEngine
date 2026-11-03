#pragma once
#include <cstdint>
#include <stdexcept>

namespace IKIGAI::RENDER {
// GPU-driven rendering capabilities. Only Vulkan and DirectX 12 implement them;
// other backends report every feature as unsupported and keep their legacy paths.
struct GpuFeatures {
	bool Compute = false;
	bool BufferUsageFlags = false;
	bool BufferTransfer = false;
	bool BufferReadback = false;
	bool DrawIndirect = false;
	bool DrawIndirectCount = false;
	bool DispatchIndirect = false;
	bool StorageImageMipViews = false;
	bool HalfFloatTextures = false;
	bool SamplerMinMaxReduction = false;
	bool SubgroupBasic = false;
	bool SubgroupBallot = false;
	bool BufferInt64Atomics = false;
	bool TimestampQueries = false;
	uint32_t SubgroupSize = 0;
	uint32_t MaxDrawIndirectCount = 0;
	uint32_t MaxComputeWorkGroupCountX = 0;
	uint32_t MaxComputeWorkGroupCountY = 0;
	uint32_t MaxComputeWorkGroupCountZ = 0;
	double TimestampPeriodNanoseconds = 0.0;
};

enum class BufferUsage : uint32_t {
	NONE = 0,
	STORAGE = 1u << 0,
	INDIRECT = 1u << 1,
	VERTEX = 1u << 2,
	INDEX = 1u << 3,
	TRANSFER_SRC = 1u << 4,
	TRANSFER_DST = 1u << 5,
	READBACK = 1u << 6,
};

constexpr BufferUsage operator|(BufferUsage left, BufferUsage right) {
	return static_cast<BufferUsage>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

constexpr bool HasBufferUsage(BufferUsage value, BufferUsage flag) {
	return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

// Layouts match VkDrawIndexedIndirectCommand / D3D12_DRAW_INDEXED_ARGUMENTS and
// VkDispatchIndirectCommand / D3D12_DISPATCH_ARGUMENTS. Shaders write them directly.
struct DrawIndexedIndirectCommand {
	uint32_t IndexCount = 0;
	uint32_t InstanceCount = 0;
	uint32_t FirstIndex = 0;
	int32_t VertexOffset = 0;
	uint32_t FirstInstance = 0;
};
static_assert(sizeof(DrawIndexedIndirectCommand) == 20);

struct DispatchIndirectCommand {
	uint32_t GroupCountX = 0;
	uint32_t GroupCountY = 0;
	uint32_t GroupCountZ = 0;
};
static_assert(sizeof(DispatchIndirectCommand) == 12);

class UnsupportedGpuFeature : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};
}
