#pragma once
#include "renderModule/backends/interface/gpuFeatures.h"
#include "renderModule/renderGraph/computeStageDescriptor.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace IKIGAI::RENDER {
class DriverInterface;
class Renderer;
class ShaderInterface;
class StorageBufferInterface;
class TextureInterface;
class UniformBufferInterface;
struct PipelineStage;
struct RenderGraphPipeline;

enum class ComputeDispatchMode { LAUNCH, TEXTURE, GROUPS, INDIRECT, MIP_CHAIN };

struct ResolvedBufferClear {
	std::string Buffer;
	uint32_t Value = 0;
	size_t Offset = 0;
	size_t Size = 0;
};

struct ResolvedAsyncImageProgramBinding {
	std::string Name;
	const std::shared_ptr<TextureInterface>* Texture = nullptr;
	std::shared_ptr<StorageBufferInterface> Buffer;
};

struct ScopedTextureDispatch {
	std::weak_ptr<ShaderInterface> Shader;
	uint64_t Generation = 0;
};

// Load-time resolution of a COMPUTE stage that uses the "Compute" descriptor block.
struct ComputeStageState {
	bool UsesProgram = false;
	bool Resolved = false;
	bool Bypassed = false;
	bool Logged = false;
	ComputeDispatchMode Mode = ComputeDispatchMode::LAUNCH;
	std::array<uint32_t, 3> Groups = { 1, 1, 1 };
	std::array<uint32_t, 3> LocalSize = { 1, 1, 1 };
	std::string DispatchTexture;
	std::string DispatchBuffer;
	size_t DispatchOffset = 0;
	std::vector<ResolvedBufferClear> Clears;
	std::vector<ResolvedAsyncImageProgramBinding> AsyncImageProgramBindings;
	std::string MipTexture;
	size_t MipOutputBinding = 0;
	bool MipHasInput = false;
	bool MipScopedBarriers = false;
	size_t MipInputBinding = 0;
	std::string MipUniform;
	uint32_t FirstMip = 0;
	uint32_t MipCount = 0;
	std::vector<std::shared_ptr<UniformBufferInterface>> MipUniforms;
	std::array<size_t, 3> MipUniformExtent = { 0, 0, 0 };
	std::optional<ScopedTextureDispatch> TextureWrite;
};

BufferUsage ResolveBufferUsage(const std::vector<PipelineBufferUsage>& usage);
// reflected compute programs predate GpuFeatures; Vulkan and DirectX 12 implement them.
bool SupportsReflectedComputePrograms(DriverInterface& driver);
// Creates the pipeline "Buffers" and records the usage each storage buffer actually received.
void CreatePipelineBuffers(RenderGraphPipeline& pipeline, Renderer& renderer);
// Validates the Compute block of a loaded COMPUTE stage, checks capabilities and applies the Unsupported policy.
void LoadComputeStage(RenderGraphPipeline& pipeline, PipelineStage& stage);
// Runs before the stage Material is loaded: an enabled Material COMPUTE stage whose Compute.Requires are unavailable is
// bypassed (BYPASS) or rejected (ERROR) without compiling its compute shader, which a backend without compute may be
// unable to build. Returns true when the stage was bypassed; the caller then skips the Material and LoadComputeStage.
bool BypassUnsupportedComputeStageBeforeLoad(RenderGraphPipeline& pipeline, PipelineStage& stage);
}
