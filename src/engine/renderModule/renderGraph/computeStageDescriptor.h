#pragma once
#include <cstddef>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <serdepp/attribute/default.hpp>
#include <serdepp/serializer.hpp>
#include <string>
#include <vector>

namespace IKIGAI::RENDER {
// Asset names of BufferUsage flags (gpuFeatures.h). Resolved to BufferUsage at pipeline load.
enum class PipelineBufferUsage { STORAGE = 0, INDIRECT, VERTEX, INDEX, TRANSFER_SRC, TRANSFER_DST, READBACK };

// RAY_QUERY keeps the historical meaning of a COMPUTE stage with a Program (dispatchRayQueries,
// requires RayQuery). COMPUTE runs the program through createComputeProgram/dispatchComputeProgram.
enum class ComputeProgramKind { RAY_QUERY = 0, COMPUTE };

// Fills Size bytes (0 = to the end) at Offset with Value before the stage dispatches.
struct BufferClearDescriptor {
	std::string Buffer;
	unsigned Value = 0;
	size_t Offset = 0;
	size_t Size = 0;

	template <class Context> constexpr static auto serde(Context& context, BufferClearDescriptor& value) {
		using Self = BufferClearDescriptor;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::Buffer, "Buffer")
			.field(&Self::Value, "Value", default_ { 0u })
			.field(&Self::Offset, "Offset", default_ { size_t { 0 } })
			.field(&Self::Size, "Size", default_ { size_t { 0 } });
	}
};

// Runs a Material compute stage once per mip of Texture. Output (and the optional Input, mip - 1) are
// storage-image uniforms bound with setStorageImage; Uniform names a uniform block that receives
// uvec4(mip, mipWidth, mipHeight, mipDepth).
struct MipChainDescriptor {
	std::string Texture;
	std::string Output;
	std::string Input;
	std::string Uniform;
	unsigned FirstMip = 0;
	unsigned MipCount = 0;

	template <class Context> constexpr static auto serde(Context& context, MipChainDescriptor& value) {
		using Self = MipChainDescriptor;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::Texture, "Texture", default_ { std::string {} })
			.field(&Self::Output, "Output", default_ { std::string {} })
			.field(&Self::Input, "Input", default_ { std::string {} })
			.field(&Self::Uniform, "Uniform", default_ { std::string {} })
			.field(&Self::FirstMip, "FirstMip", default_ { 0u })
			.field(&Self::MipCount, "MipCount", default_ { 0u });
	}
};

// Optional "Compute" block of a pipeline stage. A default block leaves the stage on its legacy path.
struct ComputeStageDescriptor {
	ComputeProgramKind ProgramKind = ComputeProgramKind::RAY_QUERY;
	std::vector<int> Groups;
	std::string DispatchBuffer;
	size_t DispatchOffset = 0;
	std::vector<BufferClearDescriptor> ClearBuffers;
	MipChainDescriptor MipChain;

	bool usesExtensions() const {
		return ProgramKind != ComputeProgramKind::RAY_QUERY || !Groups.empty() || !DispatchBuffer.empty() || !ClearBuffers.empty()
			|| !MipChain.Texture.empty();
	}

	template <class Context> constexpr static auto serde(Context& context, ComputeStageDescriptor& value) {
		using Self = ComputeStageDescriptor;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::ProgramKind, "ProgramKind", default_ { ComputeProgramKind::RAY_QUERY })
			.field(&Self::Groups, "Groups", default_ { std::vector<int> {} })
			.field(&Self::DispatchBuffer, "DispatchBuffer", default_ { std::string {} })
			.field(&Self::DispatchOffset, "DispatchOffset", default_ { size_t { 0 } })
			.field(&Self::ClearBuffers, "ClearBuffers", default_ { std::vector<BufferClearDescriptor> {} })
			.field(&Self::MipChain, "MipChain", default_ { MipChainDescriptor {} });
	}
};
}
