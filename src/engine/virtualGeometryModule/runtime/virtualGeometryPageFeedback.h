#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "virtualGeometrySelection.h"

namespace IKIGAI::RENDER {
class Renderer;
class ShaderInterface;
}

namespace IKIGAI::PROFILER {
class GpuStageProfiler;
}

namespace IKIGAI::VG {
// Scores optional refinements on the GPU. The owner keeps cut legality, page
// deduplication and I/O admission on the CPU.
class VirtualGeometryPageFeedback final {
public:
	static constexpr size_t RingSize = 4;
	static constexpr size_t MaxCandidates = 4096;
	static constexpr size_t MaxCoarseInputs = 16384;
	static constexpr size_t MaxReadbackBytes = MaxCandidates * sizeof(uint32_t);

	struct Identity {
		uint64_t Frame = 0;
		uint64_t AssetGeneration = 0;
		uint64_t ViewId = 0;
		uint64_t InstanceId = 0;
	};

	struct Candidate {
		uint32_t Transition = 0;
		std::span<const uint32_t> CoarseClusters;
	};

	struct Score {
		uint32_t Transition = 0;
		float Priority = 0.0f;
	};

	struct Batch {
		Identity Stamp;
		std::vector<Score> Scores;
		bool Dropped = false;
	};

	struct Statistics {
		uint64_t Submitted = 0;
		uint64_t Completed = 0;
		uint64_t Dropped = 0;
		uint64_t Busy = 0;
		uint64_t Overflow = 0;
		size_t Outstanding = 0;
		// Allocated slot payload bytes and outstanding logical readback bytes.
		size_t GpuBytes = 0;
		size_t ReadbackBytes = 0;
	};

	enum class SubmitStatus { Accepted, Busy, Unsupported, Overflow };

	VirtualGeometryPageFeedback();
	~VirtualGeometryPageFeedback();
	VirtualGeometryPageFeedback(const VirtualGeometryPageFeedback&) = delete;
	VirtualGeometryPageFeedback& operator=(const VirtualGeometryPageFeedback&) = delete;

	// One visible instance/view. Every candidate must have its whole coarse side
	// in the current legal cut. Spans are consumed before this call returns.
	SubmitStatus submit(RENDER::Renderer& renderer, Identity stamp, const VirtualGeometryInstanceTransform& transform,
		const VirtualGeometryView& view, float threshold, std::span<const VMeshCluster> clusters, std::span<const Candidate> candidates,
		PROFILER::GpuStageProfiler* profiler = nullptr);
	// Call on the renderer owner after beginFrame. Compare returned stamps to the
	// current asset/view/instance and revalidate transitions before requesting I/O.
	[[nodiscard]] std::vector<Batch> poll(RENDER::Renderer& renderer, uint64_t currentFrame);
	[[nodiscard]] const Statistics& statistics() const;

private:
	struct Slot;
	bool ensureShader(RENDER::Renderer& renderer);
	std::array<std::unique_ptr<Slot>, RingSize> mSlots;
	std::shared_ptr<RENDER::ShaderInterface> mShader;
	Statistics mStatistics;
	bool mShaderAttempted = false;
};
}
